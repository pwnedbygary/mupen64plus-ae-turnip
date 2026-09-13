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
	assert(strstr(last_message, "host_range=[0x1000,0x2000)") != nullptr);
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

int main()
{
	test_gate_and_jit_budget();
	test_unknown_range_budget();
	test_entry_budget_and_counters();
	test_compile_input_budget();
	return 0;
}
