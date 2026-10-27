/* Host check of the MIPS FPU compare helpers in fpu.h, which the
 * interpreters call for C.cond.S and C.cond.D.
 *
 * The four low bits of a compare's function field are its condition. Bit 0
 * selects "unordered", bit 1 "equal" and bit 2 "less than", and the compare
 * is true when a selected relation holds; a NaN operand makes the operands
 * unordered. Bit 3 makes the compare signal on a NaN: it then sets the
 * invalid-operation cause and flag bits of FCR31, which the other compares
 * leave clear for a quiet NaN. The expected results come from those bits, not
 * from fpu.h. The helpers test for a NaN with isnan() and treat every NaN as a
 * quiet one; neither they nor this check model signalling NaNs.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "device/r4300/fpu.h"
#pragma GCC diagnostic pop

/* Bionic refuses a static executable whose TLS segment is aligned to less than
 * 32 bytes on armeabi-v7a or 64 on arm64-v8a. test-float-android.sh builds this
 * file with -fno-emulated-tls, so that this variable takes real TLS and raises
 * the segment's alignment. */
__thread char g_tls_align __attribute__((aligned(64), used));

static int g_checks, g_failures;

/* c_f_d takes no operands; this gives it the others' shape. */
static void c_f_d_of(uint32_t* fcr31, const double* source, const double* target)
{
    (void)source;
    (void)target;
    c_f_d(fcr31);
}

typedef void (*compare_s)(uint32_t*, const float*, const float*);
typedef void (*compare_d)(uint32_t*, const double*, const double*);

/* Indexed by condition. */
static const struct {
    const char* name;
    compare_s s;
    compare_d d;
} g_compares[16] = {
    { "f", c_f_s, c_f_d_of },        { "un", c_un_s, c_un_d },
    { "eq", c_eq_s, c_eq_d },        { "ueq", c_ueq_s, c_ueq_d },
    { "olt", c_olt_s, c_olt_d },     { "ult", c_ult_s, c_ult_d },
    { "ole", c_ole_s, c_ole_d },     { "ule", c_ule_s, c_ule_d },
    { "sf", c_sf_s, c_sf_d },        { "ngle", c_ngle_s, c_ngle_d },
    { "seq", c_seq_s, c_seq_d },     { "ngl", c_ngl_s, c_ngl_d },
    { "lt", c_lt_s, c_lt_d },        { "nge", c_nge_s, c_nge_d },
    { "le", c_le_s, c_le_d },        { "ngt", c_ngt_s, c_ngt_d },
};

/* Operands by index: a NaN (quiet in the host's IEEE 754-2008 encoding), 1.0
 * and 2.0. Global, so that the compiler cannot know them. */
float g_singles[3];
double g_doubles[3];

static const struct {
    const char* name;
    int source, target;
    int unordered, equal, less;
} g_pairs[] = {
    { "NaN, 1.0", 0, 1, 1, 0, 0 },
    { "1.0, NaN", 1, 0, 1, 0, 0 },
    { "1.0, 1.0", 1, 1, 0, 1, 0 },
    { "1.0, 2.0", 1, 2, 0, 0, 1 },
    { "2.0, 1.0", 2, 1, 0, 0, 0 },
};

static void check(int condition, char format, const char* pair, uint32_t before,
                  uint32_t fcr31, int expected, int signals)
{
    const uint32_t invalid = FCR31_CAUSE_INVALIDOP_BIT | FCR31_FLAG_INVALIDOP_BIT;
    int result = (fcr31 & FCR31_CMP_BIT) != 0;

    g_checks += 2;
    if (result != expected) {
        fprintf(stderr, "FAIL c.%s.%c(%s), compare bit %s before: %d, expected %d\n",
                g_compares[condition].name, format, pair,
                (before & FCR31_CMP_BIT) ? "set" : "clear", result, expected);
        g_failures++;
    }
    if ((fcr31 & invalid) != (signals ? invalid : 0)) {
        fprintf(stderr, "FAIL c.%s.%c(%s), compare bit %s before: invalid-operation bits %s, "
                "expected %s\n", g_compares[condition].name, format, pair,
                (before & FCR31_CMP_BIT) ? "set" : "clear",
                (fcr31 & invalid) ? "set" : "clear", signals ? "set" : "clear");
        g_failures++;
    }
}

int main(void)
{
    const uint32_t nan_single = UINT32_C(0x7fc00000);
    const uint64_t nan_double = UINT64_C(0x7ff8000000000000);
    size_t p;
    int condition, start;

    g_tls_align = 1;
    memcpy(&g_singles[0], &nan_single, sizeof(g_singles[0]));
    g_singles[1] = 1.0f;
    g_singles[2] = 2.0f;
    memcpy(&g_doubles[0], &nan_double, sizeof(g_doubles[0]));
    g_doubles[1] = 1.0;
    g_doubles[2] = 2.0;

    for (p = 0; p < sizeof(g_pairs) / sizeof(g_pairs[0]); p++) {
        for (condition = 0; condition < 16; condition++) {
            int expected = ((condition & 1) && g_pairs[p].unordered)
                || ((condition & 2) && g_pairs[p].equal)
                || ((condition & 4) && g_pairs[p].less);
            int signals = (condition & 8) && g_pairs[p].unordered;

            /* From each state of the compare bit, so that a compare that
             * leaves it alone fails too. */
            for (start = 0; start < 2; start++) {
                const uint32_t before = start ? FCR31_CMP_BIT : 0;
                uint32_t fcr31 = before;

                g_compares[condition].s(&fcr31, &g_singles[g_pairs[p].source],
                                        &g_singles[g_pairs[p].target]);
                check(condition, 's', g_pairs[p].name, before, fcr31, expected, signals);

                fcr31 = before;
                g_compares[condition].d(&fcr31, &g_doubles[g_pairs[p].source],
                                        &g_doubles[g_pairs[p].target]);
                check(condition, 'd', g_pairs[p].name, before, fcr31, expected, signals);
            }
        }
    }

    if (g_failures != 0) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("all %d checks ok\n", g_checks);
    return 0;
}
