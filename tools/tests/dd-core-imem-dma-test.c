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
static unsigned int debug_message_count;
static char last_message[1024];
static uint32_t test_spmem[SP_MEM_SIZE / sizeof(uint32_t)];
static uint32_t test_dram_words[0x2000 / sizeof(uint32_t)];
static struct rdram test_rdram;
static struct ri_controller test_ri;
static struct mi_controller test_mi;
static struct r4300_core test_r4300;
static struct rdp_core test_dp;

rsp_plugin_functions rsp;

int DdStartupDiagnosticsEnabled(void)
{
    return diagnostics_enabled;
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
    return 0;
}