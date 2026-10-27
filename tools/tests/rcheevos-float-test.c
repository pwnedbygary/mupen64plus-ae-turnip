/* Prints how rcheevos evaluates an achievement condition that compares a
 * float in memory with a constant, for each comparison and for normal
 * numbers, NaN and infinity in memory.
 *
 * tools/test-float-android.sh builds it as the app builds rcheevos and as a
 * plain host build does, and wants the same output from both: the host build,
 * without fast math, is the reference.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_runtime_types.h"

/* Bionic refuses a static executable whose TLS segment is aligned to less than
 * 32 bytes on armeabi-v7a or 64 on arm64-v8a. test-float-android.sh builds this
 * file with -fno-emulated-tls, so that this variable takes real TLS and raises
 * the segment's alignment. */
__thread char g_tls_align __attribute__((aligned(64), used));

static uint8_t g_memory[4];

static uint32_t peek(uint32_t address, uint32_t num_bytes, void* ud)
{
    uint32_t value = 0;
    uint32_t i;

    (void)ud;
    for (i = 0; i < num_bytes && address + i < sizeof(g_memory); i++)
        value |= (uint32_t)g_memory[address + i] << (8 * i);
    return value;
}

int main(void)
{
    static const char* const operators[] = { "<", "<=", "=", "!=", ">", ">=" };
    static const struct {
        const char* name;
        uint32_t bits;
    } values[] = {
        { "50.0", UINT32_C(0x42480000) },
        { "100.0", UINT32_C(0x42c80000) },
        { "150.0", UINT32_C(0x43160000) },
        { "quiet NaN", UINT32_C(0x7fc00000) },
        { "all-ones NaN", UINT32_C(0xffffffff) },
        { "+infinity", UINT32_C(0x7f800000) },
        { "-infinity", UINT32_C(0xff800000) },
    };
    size_t v, o;

    g_tls_align = 1;
    for (v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
        /* fF reads a little-endian float. */
        g_memory[0] = (uint8_t)values[v].bits;
        g_memory[1] = (uint8_t)(values[v].bits >> 8);
        g_memory[2] = (uint8_t)(values[v].bits >> 16);
        g_memory[3] = (uint8_t)(values[v].bits >> 24);
        printf("%-12s", values[v].name);
        for (o = 0; o < sizeof(operators) / sizeof(operators[0]); o++) {
            char condition[32];
            int size;
            void* buffer;
            rc_trigger_t* trigger;

            snprintf(condition, sizeof(condition), "fF0000%sf100.0", operators[o]);
            size = rc_trigger_size(condition);
            if (size < 0) {
                fprintf(stderr, "cannot parse %s: %d\n", condition, size);
                return 1;
            }
            buffer = malloc((size_t)size);
            if (buffer == NULL)
                return 1;
            trigger = rc_parse_trigger(buffer, condition, NULL, 0);
            printf("  %s 100.0: %d", operators[o], rc_test_trigger(trigger, peek, NULL, NULL));
            free(buffer);
        }
        printf("\n");
    }
    return 0;
}
