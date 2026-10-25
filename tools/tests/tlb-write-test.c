/* Host check that TLB writes stay inside the TLB's 32 entries.
 *
 * TLBWI writes the entry that the Index register names with six bits, and
 * TLBWR the one that Random names, which the core computes from Count and
 * Wired. An Index of 32-63, or a Wired of 32 or more, named an entry past the
 * array, and a Wired of exactly 32 divided by zero. Such values reach the core
 * through MTC0 and through save states.
 *
 * The test includes pure_interp.c to reach its static TLBWI and TLBWR, which
 * share TLBWrite with the cached interpreter, with stand-ins for the
 * registers and for mapping entries. The bound comes from the hardware: the
 * TLB has 32 entries, so no write may change or map any other index. A write
 * past the array lands in the lookup tables that follow it in struct tlb, so
 * the test also wants those untouched. Which entry, if any, a write with an
 * Index of 32-63 or a Wired of 32 or more picks follows upstream
 * mupen64plus-core e6f8882701 and 1c3d9326a2, not a hardware reference.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "device/r4300/pure_interp.c"
#pragma GCC diagnostic pop

static int g_failures;

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            g_failures++;                                                   \
        }                                                                   \
    } while (0)

void DebugMessage(int level, const char *message, ...)
{
    (void)level;
    (void)message;
}

static struct r4300_core g_r4300;
static uint32_t g_pc;
static int g_maps, g_unmaps;

uint32_t* r4300_cp0_regs(struct cp0* cp0) { return cp0->regs; }
uint32_t* r4300_pc(struct r4300_core* r4300) { (void)r4300; return &g_pc; }
void cp0_update_count(struct r4300_core* r4300) { (void)r4300; }
void tlb_map(struct tlb* tlb, size_t entry) { (void)tlb; (void)entry; g_maps++; }
void tlb_unmap(struct tlb* tlb, size_t entry) { (void)tlb; (void)entry; g_unmaps++; }

static uint32_t* regs(void)
{
    return g_r4300.cp0.regs;
}

static void reset(void)
{
    memset(&g_r4300, 0, sizeof(g_r4300));
    g_r4300.emumode = EMUMODE_PURE_INTERPRETER;
    g_r4300.cp0.count_per_op = 1;
    regs()[CP0_ENTRYHI_REG] = UINT32_C(0x12346000);
    regs()[CP0_ENTRYLO0_REG] = UINT32_C(0x00000046);
    regs()[CP0_ENTRYLO1_REG] = UINT32_C(0x00000087);
    g_maps = 0;
    g_unmaps = 0;
}

static int lookup_tables_untouched(void)
{
    static const struct tlb zero;
    return memcmp(g_r4300.cp0.tlb.LUT_r, zero.LUT_r, sizeof(zero.LUT_r)) == 0
        && memcmp(g_r4300.cp0.tlb.LUT_w, zero.LUT_w, sizeof(zero.LUT_w)) == 0;
}

static void report(const char *run, int failures_before)
{
    printf("%s: %s\n", run, g_failures == failures_before ? "ok" : "FAILED");
    fflush(stdout);
}

static void check_tlbwi(uint32_t index, int writes)
{
    char run[80];
    int before = g_failures;

    reset();
    regs()[CP0_INDEX_REG] = index;
    TLBWI(&g_r4300, 0);
    CHECK(g_maps == writes && g_unmaps == writes);
    if (writes)
        CHECK(g_r4300.cp0.tlb.entries[index & 0x3f].vpn2 == (UINT32_C(0x12346000) >> 13));
    CHECK(lookup_tables_untouched());
    snprintf(run, sizeof(run), "TLBWI with Index %u %s", index,
             writes ? "writes that entry" : "writes nothing");
    report(run, before);
}

static void check_tlbwr(uint32_t wired, uint32_t count, int writes)
{
    char run[80];
    int before = g_failures;

    reset();
    regs()[CP0_WIRED_REG] = wired;
    regs()[CP0_COUNT_REG] = count;
    TLBWR(&g_r4300, 0);
    CHECK(g_maps == writes && g_unmaps == writes);
    CHECK(lookup_tables_untouched());
    snprintf(run, sizeof(run), "TLBWR with Wired %u and Count %u %s", wired, count,
             writes ? "writes an entry" : "writes nothing");
    report(run, before);
}

int main(void)
{
    check_tlbwi(5, 1);
    check_tlbwi(31, 1);
    check_tlbwi(32, 0);
    check_tlbwi(63, 0);
    check_tlbwr(0, 1000, 1);
    check_tlbwr(31, 1000, 1);
    /* With Wired past 31, upstream's Random counts over 0-63 (e6f8882701). */
    check_tlbwr(40, 10, 1);
    check_tlbwr(40, 40, 0);
    /* Last, as a Wired of 32 used to divide by zero. */
    check_tlbwr(32, 40, 0);
    return g_failures != 0;
}
