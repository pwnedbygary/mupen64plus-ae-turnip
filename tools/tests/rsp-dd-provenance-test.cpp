#include "rsp_diag.hpp"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned message_count;
static unsigned jit_count;
static unsigned unknown_count;
static unsigned compile_word_lines;
static unsigned dma_lines;
static unsigned entry_count;
static unsigned exhaustion_count;
static unsigned bad_messages;
static int last_level;
static char last_message[1024];

static void capture(void *, int level, const char *message)
{
	assert(message != nullptr);
	assert(level == 3);
	last_level = level;
	++message_count;
	snprintf(last_message, sizeof(last_message), "%s", message);
	if (strstr(message, "jit_region ") != nullptr
	    && strstr(message, " record=") != nullptr)
		++jit_count;
	if (strstr(message, "cache-range-unavailable") != nullptr)
		++unknown_count;
	if (strstr(message, "jit_compile_input") != nullptr)
		++compile_word_lines;
	if (strstr(message, "DDSTART11 RSP dma_read record=") != nullptr)
		++dma_lines;
	if (strstr(message, "RSP entry") != nullptr)
		++entry_count;
	if (strstr(message, "exhaustion") != nullptr)
		++exhaustion_count;
	if (strlen(message) >= sizeof(last_message))
		++bad_messages;
}

static void reset_capture()
{
	message_count = 0;
	jit_count = 0;
	unknown_count = 0;
	compile_word_lines = 0;
	dma_lines = 0;
	entry_count = 0;
	exhaustion_count = 0;
	bad_messages = 0;
	last_level = 0;
	memset(last_message, 0, sizeof(last_message));
}

static void test_gate_and_jit_budget()
{
	reset_capture();
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	assert(!RSP::Diagnostics::enabled());
	assert(!RSP::Diagnostics::trace_jit(0x1000, 0x2000, 0, 1, 1, "disabled"));
	assert(message_count == 0);

	RSP::Diagnostics::set_callback(capture, nullptr);
	assert(RSP::Diagnostics::enabled());
	assert(!RSP::Diagnostics::trace_jit(0x2000, 0x1000, 0, 1, 1, "bad-range"));
	assert(!RSP::Diagnostics::trace_jit(0x1000, 0x2000, 0xffc, 2, 1,
	                                    "bad-imem-range"));
	assert(RSP::Diagnostics::trace_jit(0x1000, 0x2000, 0, 1, 0, "before-reset"));
	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	assert(RSP::Diagnostics::trace_jit(0x1000, 0x2000, 0, 1, 1, "after-reset"));
	assert(jit_count == 1);
assert(strstr(last_message, "allocation_range=[0x1000,0x2000)") != nullptr);
	for (unsigned i = 1; i < 2048; ++i)
		assert(RSP::Diagnostics::trace_jit(
		    0x1000, 0x2000, 0, 1, i, "commit"));
	assert(!RSP::Diagnostics::trace_jit(0x1000, 0x2000, 0, 1, 0, "exhausted"));
	assert(!RSP::Diagnostics::trace_jit(0x1000, 0x2000, 0, 1, 0, "exhausted"));
	assert(jit_count == 2048);
	assert(exhaustion_count == 1);
	assert(message_count == 2049);
	assert(strstr(last_message, "jit_region exhaustion: limit=2048") != nullptr);
	assert(bad_messages == 0);
}

static void test_unknown_range_budget()
{
	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	for (unsigned i = 0; i < 2048; ++i)
		assert(RSP::Diagnostics::trace_jit_range_unavailable(0x1000 + i));
	assert(!RSP::Diagnostics::trace_jit_range_unavailable(0x1000));
	assert(unknown_count == 2048);
	assert(exhaustion_count == 1);
	assert(message_count == 2049);
}

static void test_entry_budget_and_counters()
{
	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	assert(RSP::Diagnostics::trace_rsp_entry(1, 0x100, 1, 0x40));
	assert(strstr(last_message, "entry_count=1 return_count=0") != nullptr);
	RSP::Diagnostics::rsp_return();
	assert(RSP::Diagnostics::trace_rsp_entry(1, 0x100, 1, 0x40));
	RSP::Diagnostics::rsp_return();
	assert(RSP::Diagnostics::trace_rsp_entry(2, 0x100, 1, 0x40));
	assert(strstr(last_message, "entry_count=3 return_count=2") != nullptr);
	RSP::Diagnostics::rsp_return();
	assert(entry_count == 2);

	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	assert(RSP::Diagnostics::trace_rsp_entry(1, 0x100, 1, 0x40));
	assert(strstr(last_message, "entry_count=1 return_count=0") != nullptr);
	RSP::Diagnostics::rsp_return();
	assert(RSP::Diagnostics::trace_rsp_entry(2, 0x100, 1, 0x40));
	assert(strstr(last_message, "entry_count=2 return_count=1") != nullptr);
	RSP::Diagnostics::rsp_return();

	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	for (unsigned i = 0; i < 128; ++i)
	{
		assert(RSP::Diagnostics::trace_rsp_entry(
		    i + 1, 0x100, 1, 0x40));
		RSP::Diagnostics::rsp_return();
	}
	assert(RSP::Diagnostics::trace_rsp_entry(129, 0x100, 1, 0x40));
	assert(RSP::Diagnostics::trace_rsp_entry(130, 0x100, 1, 0x40));
	assert(entry_count == 128);
	assert(exhaustion_count == 1);
	assert(message_count == 129);
	assert(strstr(last_message, "rsp_entry exhaustion: limit=128") != nullptr);
	assert(bad_messages == 0);
}

static void test_compile_input_budget()
{
	std::vector<uint32_t> words(1024);
	for (unsigned i = 0; i < words.size(); ++i)
		words[i] = 0x10000000u + i;

	reset_capture();
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	assert(!RSP::Diagnostics::trace_jit_compile_words(
	    0x1000, 0, nullptr, 1));

	RSP::Diagnostics::set_callback(capture, nullptr);
	assert(RSP::Diagnostics::trace_jit_compile_words(
	    0x1000, 0, words.data(), 65));
	assert(compile_word_lines == 2);
	assert(strstr(last_message, "word_count=1") != nullptr);
assert(strstr(last_message, "imem_start_pc=0x100") != nullptr);
assert(strstr(last_message, "region_host_start=0x1000") != nullptr);

	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	for (unsigned i = 0; i < 32; ++i)
		assert(RSP::Diagnostics::trace_jit_compile_words(
		    0x1000 + i, 0, words.data(), words.size()));
	assert(!RSP::Diagnostics::trace_jit_compile_words(
	    0x2000, 0, words.data(), 1));
	assert(compile_word_lines == 512);
	assert(exhaustion_count == 1);
	assert(message_count == 513);
	assert(bad_messages == 0);
}

static void test_dma_observation()
{
	RSP::Diagnostics::DmaReadObservation observation = {};
	uint32_t task_words[16];
	for (unsigned i = 0; i < 16; ++i)
		task_words[i] = 0xabc00000u + i;

	observation.raw_dma_cache = 0x1ffc;
	observation.raw_dma_dram = 0x7ffffc;
	observation.raw_read_length = 0x12345678;
	observation.requested_length = 5;
	observation.aligned_length = 8;
	observation.effective_length = 4;
	observation.count = 1;
	observation.transfer_count = 2;
	observation.skip = 0x20;
	observation.payload_hash = 0x1122334455667788ull;
	observation.payload_word_count = 2;
	observation.payload_first_count = 2;
	observation.payload_first[0] = 0x11111111;
	observation.payload_first[1] = 0x22222222;
	observation.payload_last_count = 2;
	observation.payload_last[0] = 0x11111111;
	observation.payload_last[1] = 0x22222222;
	observation.source_first_count = 2;
	observation.source_first[0] = 0x7ffffc;
	observation.source_first[1] = 0x000000;
	observation.source_last_count = 2;
	observation.source_last[0] = 0x7ffffc;
	observation.source_last[1] = 0x000000;
	observation.imem_before_hash = 0x0102030405060708ull;
	observation.imem_after_hash = 0x1112131415161718ull;
	observation.imem_before_samples[0] = 0x00000000;
	observation.imem_before_samples[2] = 0xf60f60f6;
	observation.imem_after_samples[0] = 0xdeadbeef;
	observation.imem_after_samples[2] = 0xcafebabe;
	observation.first_imem_start = 0x1ff0;
	observation.first_imem_end = 0x1ff8;
	observation.imem_min_start = 0x1000;
	observation.imem_max_end = 0x2000;
	observation.imem_bank_mask = 0x2;
	observation.imem_range_count = 2;
	observation.imem_write_word_count = 2;
	observation.payload_samples_truncated = true;

	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	assert(RSP::Diagnostics::trace_rsp_entry(
	    1, 2, 3, 0x40, task_words));
	RSP::Diagnostics::rsp_return();
	RSP::Diagnostics::trace_rsp_dma_read(observation);
	assert(dma_lines == 1);
	assert(strstr(last_message, "raw_dma_cache=0x00001ffc") != nullptr);
	assert(strstr(last_message, "requested_length=5 aligned_length=8 effective_length=4") != nullptr);
	assert(strstr(last_message, "first_imem_range=0x1ff0-0x1ff8") != nullptr);
	assert(strstr(last_message, "source_first=[0x7ffffc,0x000000]") != nullptr);
	assert(strstr(last_message, "task_snapshot_valid=1") != nullptr);
	assert(strstr(last_message, "abc00000") != nullptr);

	RSP::Diagnostics::trace_rsp_dma_read(observation);
	assert(dma_lines == 1);
	observation.payload_hash++;
	RSP::Diagnostics::trace_rsp_dma_read(observation);
	assert(dma_lines == 2);
	assert(strstr(last_message, "imem_dma_sequence=3") != nullptr);

	reset_capture();
	RSP::Diagnostics::set_callback(capture, nullptr);
	for (unsigned i = 0; i < 512; ++i)
	{
		observation.raw_dma_dram = i;
		observation.payload_hash = i;
		RSP::Diagnostics::trace_rsp_dma_read(observation);
	}
	observation.raw_dma_dram = 512;
	observation.payload_hash = 512;
	RSP::Diagnostics::trace_rsp_dma_read(observation);
	assert(dma_lines == 512);
	assert(exhaustion_count == 1);
	assert(strstr(last_message, "DDSTART11 RSP dma_read exhaustion") != nullptr);
}

static void test_dma_eligibility()
{
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	assert(!RSP::Diagnostics::imem_dma_eligible(0x0000, 0x1000, 1));
	assert(!RSP::Diagnostics::imem_dma_eligible(0x0ff8, 0x0008, 1));
	assert(RSP::Diagnostics::imem_dma_eligible(0x0ff8, 0x0008, 2));
	assert(RSP::Diagnostics::imem_dma_eligible(0x1000, 0x0008, 1));
	assert(!RSP::Diagnostics::enabled());
}

int main()
{
	test_gate_and_jit_budget();
	test_unknown_range_budget();
	test_entry_budget_and_counters();
	test_compile_input_budget();
	test_dma_observation();
	test_dma_eligibility();
	return 0;
}
