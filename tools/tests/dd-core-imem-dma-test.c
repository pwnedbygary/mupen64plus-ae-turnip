#include "device/rcp/rsp/rsp_core.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/fb.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/rdram/rdram.h"
#include "plugin/plugin.h"

static int diagnostics_enabled;
static unsigned int ddstart11_count;
static unsigned int exhaustion_count;
static unsigned int ddstart12_count;
static unsigned int ddstart13_count;
static unsigned int ddstart13_exhaustion_count;
static unsigned int rsp_cycles_calls;
static unsigned int debug_message_count;
static char last_message[1024];
static char last_launch_message[1024];
static char last_launch_exhaustion_message[1024];
static uint32_t test_spmem[SP_MEM_SIZE / sizeof(uint32_t)];
static uint32_t test_dram_words[0x2000 / sizeof(uint32_t)];
static struct rdram test_rdram;
static struct rdram null_rdram; /* dram stays NULL: the fail-closed case */
static struct ri_controller test_ri;
static struct mi_controller test_mi;
static struct r4300_core test_r4300;
static struct rdp_core test_dp;

rsp_plugin_functions rsp;

static int policy_enabled;

int DdStartupDiagnosticsEnabled(void)
{
    return diagnostics_enabled;
}

int DdRuntimePolicyGet(void)
{
    return policy_enabled;
}

static void capture_dd_message(void *context, int level, const char *message)
{
    (void)context;

    assert(level == M64MSG_INFO);
    snprintf(last_message, sizeof(last_message), "%s", message);
    if (strstr(last_message, "DDSTART11 RSP CPU->IMEM") != NULL)
        ++ddstart11_count;
    if (strstr(last_message, "DDSTART11 IMEMDMA exhaustion") != NULL)
        ++exhaustion_count;
    if (strstr(last_message, "DDSTART12 RSP cmd_entry") != NULL)
        ++ddstart12_count;
    if (strstr(last_message, "DDSTART13 RSP launch sequence=") != NULL) {
        ++ddstart13_count;
        snprintf(last_launch_message, sizeof(last_launch_message), "%s",
                 last_message);
    }
    if (strstr(last_message, "DDSTART13 RSP launch_exhausted") != NULL) {
        ++ddstart13_exhaustion_count;
        snprintf(last_launch_exhaustion_message,
                 sizeof(last_launch_exhaustion_message), "%s", last_message);
    }
}

ptr_DdStartupDiagnosticsCallback DdStartupDiagnosticsGetCallback(void **context)
{
    if (context != NULL)
        *context = NULL;
    return diagnostics_enabled ? capture_dd_message : NULL;
}

void DebugMessage(int level, const char *format, ...)
{
    (void)level;
    (void)format;
    ++debug_message_count;
}

static unsigned int do_rsp_cycles_stub(unsigned int cycles)
{
    (void)cycles;
    ++rsp_cycles_calls;
    return 0;
}

void cp0_update_count(struct r4300_core *r4300)
{
    (void)r4300;
}

void add_interrupt_event(struct cp0 *cp0, int type, unsigned int delay)
{
    (void)cp0;
    (void)type;
    (void)delay;
}

unsigned int *get_event(const struct interrupt_queue *queue, int type)
{
    (void)queue;
    (void)type;
    return NULL;
}

void clear_rcp_interrupt(struct mi_controller *mi, uint32_t mi_intr)
{
    (void)mi;
    (void)mi_intr;
}

void signal_rcp_interrupt(struct mi_controller *mi, uint32_t mi_intr)
{
    (void)mi;
    (void)mi_intr;
}

void raise_rcp_interrupt(struct mi_controller *mi, uint32_t mi_intr)
{
    (void)mi;
    (void)mi_intr;
}

void pre_framebuffer_read(struct fb *fb, uint32_t address)
{
    (void)fb;
    (void)address;
}

void post_framebuffer_write(struct fb *fb, uint32_t address, uint32_t length)
{
    (void)fb;
    (void)address;
    (void)length;
}

void protect_framebuffers(struct fb *fb)
{
    (void)fb;
}

void unprotect_framebuffers(struct fb *fb)
{
    (void)fb;
}

void new_frame(void)
{
}

static void submit_cpu_to_imem_dma(struct rsp_core *sp,
                                   uint32_t memaddr,
                                   uint32_t dramaddr,
                                   uint32_t length)
{
    write_rsp_regs(sp, 0x00, memaddr, UINT32_MAX);
    write_rsp_regs(sp, 0x04, dramaddr, UINT32_MAX);
    write_rsp_regs(sp, 0x08, length, UINT32_MAX);
    rsp_end_of_dma_event(sp);
}

int main(void)
{
    struct rsp_core sp;
    unsigned int i;

    memset(test_spmem, 0, sizeof(test_spmem));
    memset(test_dram_words, 0, sizeof(test_dram_words));
    memset(&test_rdram, 0, sizeof(test_rdram));
    memset(&test_ri, 0, sizeof(test_ri));
    memset(&test_mi, 0, sizeof(test_mi));
    memset(&test_r4300, 0, sizeof(test_r4300));
    memset(&test_dp, 0, sizeof(test_dp));
    test_rdram.dram = test_dram_words;
    test_rdram.dram_size = sizeof(test_dram_words);
    test_ri.rdram = &test_rdram;
    test_mi.r4300 = &test_r4300;
    test_r4300.rdram = &test_rdram;
    init_rsp(&sp, test_spmem, &test_mi, &test_dp, &test_ri);
    poweron_rsp(&sp);

    /* DD-off execution must not emit an observer record. */
    diagnostics_enabled = 0;
    submit_cpu_to_imem_dma(&sp, 0x1000, 0, 7);
    assert(ddstart11_count == 0);
    assert(debug_message_count == 0);

    diagnostics_enabled = 1;
    for (i = 0; i < 16; ++i)
        ((unsigned char *)test_dram_words)[i ^ S8] = (unsigned char)(0xa0 + i);

    /*
     * Consume a simulated DDSTART1 DebugMessage startup budget.  DDSTART11
     * goes through the direct callback and must remain visible.
     */
    for (i = 0; i < 256; ++i)
        DebugMessage(M64MSG_INFO, "startup");
    assert(debug_message_count == 256);
    submit_cpu_to_imem_dma(&sp, 0x1000, 0, 7);
    assert(ddstart11_count == 1);
    assert(strstr(last_message, "raw={m=0x00001000") != NULL);
    assert(strstr(last_message, "l=0x00000007} decoded={l=8 c=1 s=0}") != NULL);
    assert(strstr(last_message, "dest={bank=IMEM range=[0x1000,0x1008)") != NULL);
    assert(strstr(last_message, "payload={h=0x") != NULL);
    assert(strstr(last_message, "before={h=0x") != NULL);
    assert(strstr(last_message, "after={h=0x") != NULL);

    /*
     * The second copy has a distinct before-image; a third copy then repeats
     * that complete raw/payload/before/after identity and is deduplicated.
     */
    submit_cpu_to_imem_dma(&sp, 0x1000, 0, 7);
    assert(ddstart11_count == 2);
    submit_cpu_to_imem_dma(&sp, 0x1000, 0, 7);
    assert(ddstart11_count == 2);

    /* A DMEM-originating copy which crosses into IMEM is eligible. */
    submit_cpu_to_imem_dma(&sp, 0x0ff8, 0, 15);
    assert(ddstart11_count == 3);
    assert(strstr(last_message,
                  "dest={bank=DMEM range=[0x0ff8,0x1008)"
                  " imem=[0x1000,0x1008) probe=valid}") != NULL);

    /* Distinct source payloads remain observable until the 256-record cap. */
    poweron_rsp(&sp);
    {
        unsigned int records_before = ddstart11_count;

        for (i = 1; i < 257; ++i) {
            ((unsigned char *)test_dram_words)[(i * 8) ^ S8] =
                (unsigned char)i;
            submit_cpu_to_imem_dma(&sp, 0x1000, i * 8, 7);
        }
        assert(ddstart11_count == records_before + 256);
        submit_cpu_to_imem_dma(&sp, 0x1000, 0, 7);
        assert(exhaustion_count == 1);
        assert(strstr(last_message, "reason=record-budget") != NULL);
    }

    /* Repeated exhausted identities do not emit another marker per frame. */
    submit_cpu_to_imem_dma(&sp, 0x1000, 0, 7);
    assert(exhaustion_count == 1);
    assert(debug_message_count == 256);

    /*
     * P08c-writer (2/2): the submission-time command-buffer hash.  An audio
     * task entry under the DD policy emits one DDSTART12 line carrying the
     * word-wise FNV of the buffer named by the task words and its non-zero
     * word count.  The expectation is computed here from the pattern this
     * test installs, with the FNV constants declared locally.
     */
    {
        uint32_t buffer_word_index = 0x1000 / 4;
        uint64_t expected = 0xcbf29ce484222325ull;
        unsigned int k;

        test_spmem[0xfc0 / 4 + 0] = 2u;            /* audio task */
        test_spmem[0xfc0 / 4 + 12] = 0x80001000u;  /* KSEG0 mirror of 0x1000 */
        test_spmem[0xfc0 / 4 + 13] = 0x40u;        /* 16 words */

        for (k = 0; k < 16; ++k) {
            test_dram_words[buffer_word_index + k] = 0x11110000u + k;
            expected = (expected * 0x100000001b3ull) ^ (0x11110000u + k);
        }
        policy_enabled = 1;
        diagnostics_enabled = 1;
        last_message[0] = '\0';
        dd_cmd_entry_hash_observe(&sp);
        assert(strstr(last_message, "DDSTART12 RSP cmd_entry") != NULL);
        {
            char wanted[128];
            snprintf(wanted, sizeof(wanted),
                     "hash=0x%016llx nonzero_words=16 words=16",
                     (unsigned long long) expected);
            assert(strstr(last_message, wanted) != NULL);
        }

        /* An all-zero buffer reports zero non-zero words and the zero hash.
         * The hash restarts per observation (FNV is sequential), so the
         * expectation restarts at the offset basis too. */
        expected = 0xcbf29ce484222325ull;
        for (k = 0; k < 16; ++k) {
            test_dram_words[buffer_word_index + k] = 0u;
            expected = (expected * 0x100000001b3ull); /* XOR 0 is a no-op */
        }
        last_message[0] = '\0';
        dd_cmd_entry_hash_observe(&sp);
        {
            char wanted[128];
            snprintf(wanted, sizeof(wanted),
                     "hash=0x%016llx nonzero_words=0 words=16",
                     (unsigned long long) expected);
            assert(strstr(last_message, wanted) != NULL);
        }

        /* Both gates are required: the policy and the diagnostics callback. */
        policy_enabled = 0;
        last_message[0] = '\0';
        dd_cmd_entry_hash_observe(&sp);
        assert(last_message[0] == '\0');
        policy_enabled = 1;
        diagnostics_enabled = 0;
        last_message[0] = '\0';
        dd_cmd_entry_hash_observe(&sp);
        assert(last_message[0] == '\0');

        /* A non-audio task word block is not the audio path's business: the
         * observer is called from the audio branch, and its own contract is
         * to hash whatever buffer the words name, so this case only pins
         * that a zero-size buffer emits nothing. */
        diagnostics_enabled = 1;
        test_spmem[0xfc0 / 4 + 13] = 0u;
        last_message[0] = '\0';
        dd_cmd_entry_hash_observe(&sp);
        assert(last_message[0] == '\0');
        test_spmem[0xfc0 / 4 + 13] = 0x40u;

        /* A NULL backing store fails closed rather than dereferencing.
         * dram_size is set non-zero on purpose: with it zero the bounds
         * check would return first and this case would pass whether or not
         * the dram==NULL guard exists (a review caught exactly that). */
        {
            struct rdram *saved = test_r4300.rdram;
            null_rdram.dram_size = 0x2000u;
            test_r4300.rdram = &null_rdram;
            last_message[0] = '\0';
            dd_cmd_entry_hash_observe(&sp);
            assert(last_message[0] == '\0');
            test_r4300.rdram = saved;
        }

        /* Out-of-RDRAM sizes emit nothing rather than reading past the
         * backing store. */
        test_spmem[0xfc0 / 4 + 12] = 0x80000000u; /* physical 0 */
        test_spmem[0xfc0 / 4 + 13] = 0x100000u;   /* 1 MiB > 8 KiB backing */
        last_message[0] = '\0';
        dd_cmd_entry_hash_observe(&sp);
        assert(last_message[0] == '\0');
    }

    /*
     * P08c-writer (2/2) call-site coverage: the observation must fire from
     * do_SP_Task's AUDIO branch (before the RSP runs) and never for a task
     * whose type dispatch does not select that branch.  The third branch
     * (type 3) is the negative case because it needs no framebuffer stubs;
     * a mutation that hoists the call above the type dispatch would emit
     * there and fail this assertion.
     */
    rsp.doRspCycles = do_rsp_cycles_stub;
    {
        const unsigned int entries_before = ddstart12_count;
        const unsigned int cycles_before = rsp_cycles_calls;

        test_spmem[0xfc0 / 4 + 0] = 2u; /* audio */
        test_spmem[0xfc0 / 4 + 12] = 0x80001000u;
        test_spmem[0xfc0 / 4 + 13] = 0x40u;
        do_SP_Task(&sp);
        assert(ddstart12_count == entries_before + 1);
        assert(rsp_cycles_calls == cycles_before + 1);

        test_spmem[0xfc0 / 4 + 0] = 3u; /* not audio, not graphics */
        do_SP_Task(&sp);
        assert(ddstart12_count == entries_before + 1);
        assert(rsp_cycles_calls == cycles_before + 2);
    }

    /*
     * P08c-writer (part 3): exercise the actual guest SP_STATUS write seam.
     * DDSTART13 is emitted only after an asserted HALT is cleared and before
     * do_SP_Task executes.  It carries the complete 16-word descriptor and
     * the same command-buffer hash/non-zero count as DDSTART12.  Its stream
     * is independent of the legacy DebugMessage budget and must retain more
     * than the 880 entries expected in the native run.
     */
    {
        unsigned int launch_before;
        unsigned int cycles_before;
        unsigned int exhaustion_before;
        unsigned int k;
        uint64_t expected = UINT64_C(14695981039346656037);

        poweron_rsp(&sp);
        diagnostics_enabled = 1;
        policy_enabled = 1;
        test_spmem[0xfc0 / 4 + 0] = 2u;
        test_spmem[0xfc0 / 4 + 12] = 0x80001000u;
        test_spmem[0xfc0 / 4 + 13] = 0x40u;
        for (k = 0; k < 16; ++k) {
            test_dram_words[0x1000 / 4 + k] = 0x21210000u + k;
            expected = (expected * UINT64_C(1099511628211))
                ^ (0x21210000u + k);
        }

        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        last_launch_message[0] = '\0';
        launch_before = ddstart13_count;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 1);
        assert(strstr(last_launch_message, "sequence=1 type=2") != NULL);
        assert(strstr(last_launch_message,
                      "status_before=0x00000001 status_after=0x00000000") != NULL);
        assert(strstr(last_launch_message,
                      "descriptor={w0=0x00000002") != NULL);
        assert(strstr(last_launch_message, "w12=0x80001000") != NULL);
        assert(strstr(last_launch_message, "w13=0x00000040") != NULL);
        assert(strstr(last_launch_message, "w15=0x00000000}") != NULL);
        {
            char wanted[160];
            snprintf(wanted, sizeof(wanted),
                     "valid=1 data_ptr=0x00001000 data_size=0x00000040 "
                     "hash=0x%016llx nonzero_words=16 words=16",
                     (unsigned long long)expected);
            assert(strstr(last_launch_message, wanted) != NULL);
        }
        assert(strlen(last_launch_message) < sizeof(last_launch_message) - 1);

        /* An invalid command range still records the full descriptor but
         * never hashes outside the RDRAM backing. */
        launch_before = ddstart13_count;
        test_spmem[0xfc0 / 4 + 12] = 0x80001ff0u;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 1);
        assert(strstr(last_launch_message, "sequence=2 type=2") != NULL);
        assert(strstr(last_launch_message,
                      "w12=0x80001ff0") != NULL);
        assert(strstr(last_launch_message,
                      "buffer={valid=0 data_ptr=0x00001ff0") != NULL);
        test_spmem[0xfc0 / 4 + 12] = 0x80001000u;

        /* A non-audio launch is outside DDSTART13's command-buffer scope. */
        launch_before = ddstart13_count;
        test_spmem[0xfc0 / 4 + 0] = 1u;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before);

        /*
         * DMA busy/full remains a qualifying HALT-clear execution boundary.
         * The status flags are recorded rather than filtered, and execution
         * is asserted unchanged through the RSP-cycle stub.
         */
        test_spmem[0xfc0 / 4 + 0] = 2u;
        launch_before = ddstart13_count;
        cycles_before = rsp_cycles_calls;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT | SP_STATUS_DMA_BUSY;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 1);
        assert(rsp_cycles_calls == cycles_before + 1);
        assert(strstr(last_launch_message, "sequence=3 type=2") != NULL);
        assert(strstr(last_launch_message,
                      "busy=1 dma_busy=1 dma_full=0") != NULL);

        launch_before = ddstart13_count;
        cycles_before = rsp_cycles_calls;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT | SP_STATUS_DMA_FULL;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 1);
        assert(rsp_cycles_calls == cycles_before + 1);
        assert(strstr(last_launch_message,
                      "sequence=4 type=2") != NULL);
        assert(strstr(last_launch_message,
                      "busy=1 dma_busy=0 dma_full=1") != NULL);

        /* A null RDRAM backing remains a production-path launch record with
         * a fail-closed buffer field, and it must still execute the task. */
        {
            struct rdram *saved_rdram = test_r4300.rdram;

            null_rdram.dram_size = sizeof(test_dram_words);
            test_r4300.rdram = &null_rdram;
            launch_before = ddstart13_count;
            cycles_before = rsp_cycles_calls;
            sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
            write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
            assert(ddstart13_count == launch_before + 1);
            assert(rsp_cycles_calls == cycles_before + 1);
            assert(strstr(last_launch_message,
                          "sequence=5 type=2") != NULL);
            assert(strstr(last_launch_message,
                          "buffer={valid=0 data_ptr=0x00001000") != NULL);
            test_r4300.rdram = saved_rdram;
        }

        /* Both gates suppress the record. */
        launch_before = ddstart13_count;
        diagnostics_enabled = 0;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before);
        diagnostics_enabled = 1;
        policy_enabled = 0;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before);
        policy_enabled = 1;

        /* A fresh RSP lifecycle starts a fresh, stable bounded sequence. */
        poweron_rsp(&sp);
        launch_before = ddstart13_count;
        test_spmem[0xfc0 / 4 + 0] = 2u;
        for (k = 0; k < 900; ++k) {
            sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
            write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        }
        assert(ddstart13_count == launch_before + 900);
        assert(strstr(last_launch_message, "sequence=900 type=2") != NULL);

        /* Reset must restart the sequence rather than retaining old records. */
        poweron_rsp(&sp);
        test_spmem[0xfc0 / 4 + 0] = 2u;
        test_spmem[0xfc0 / 4 + 12] = 0x80001000u;
        test_spmem[0xfc0 / 4 + 13] = 0x40u;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 901);
        assert(strstr(last_launch_message, "sequence=1 type=2") != NULL);

        /*
         * The bounded launch class emits all 2048 records, then exactly one
         * exhaustion marker for the next qualifying launch.  Later launches
         * are suppressed until the next lifecycle reset.
         */
        poweron_rsp(&sp);
        test_spmem[0xfc0 / 4 + 0] = 2u;
        test_spmem[0xfc0 / 4 + 12] = 0x80001000u;
        test_spmem[0xfc0 / 4 + 13] = 0x40u;
        launch_before = ddstart13_count;
        exhaustion_before = ddstart13_exhaustion_count;
        for (k = 0; k < 2048; ++k) {
            sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
            write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        }
        assert(ddstart13_count == launch_before + 2048);
        assert(ddstart13_exhaustion_count == exhaustion_before);
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 2048);
        assert(ddstart13_exhaustion_count == exhaustion_before + 1);
        assert(strstr(last_launch_exhaustion_message,
                      "DDSTART13 RSP launch_exhausted sequence=2049 "
                      "records=2048 limit=2048") != NULL);
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 2048);
        assert(ddstart13_exhaustion_count == exhaustion_before + 1);

        /* Reset after actual exhaustion clears both the marker latch and the
         * sequence, without changing execution. */
        poweron_rsp(&sp);
        test_spmem[0xfc0 / 4 + 0] = 2u;
        test_spmem[0xfc0 / 4 + 12] = 0x80001000u;
        test_spmem[0xfc0 / 4 + 13] = 0x40u;
        launch_before = ddstart13_count;
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        write_rsp_regs(&sp, 0x10, SP_STATUS_HALT, UINT32_MAX);
        assert(ddstart13_count == launch_before + 1);
        assert(ddstart13_exhaustion_count == exhaustion_before + 1);
        assert(strstr(last_launch_message, "sequence=1 type=2") != NULL);
    }

    return 0;
}