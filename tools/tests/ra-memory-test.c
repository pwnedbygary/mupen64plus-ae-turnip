/*
 * Host fixture for the RetroAchievements memory read callback.
 *
 * rcheevos asks the read callback for N64 RAM by the addresses in its
 * console memory map (rc_console_memory_regions), and each region also
 * gives the real address of its first byte. Expectations come from that
 * map and from the N64 address map: a KSEG0 real address is physical
 * address (real - 0x80000000), and RDRAM starts at physical MM_RDRAM_DRAM.
 * The emulator keeps RDRAM at mem_base_u32(base, MM_RDRAM_DRAM)
 * (device.c), so a read must return the bytes stored there.
 *
 * ra_glue_read_memory() and mem_base_u32() are included from the
 * production sources, and the reads are checked in both memory base modes.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "main/main.h"
#include "rc_consoles.h"

void* g_mem_base;

void DebugMessage(int level, const char* message, ...)
{
    (void)level;
    (void)message;
}

/* Production memory base layout and mem_base_u32(). The debug-only paths in
 * memory.c leave parameters unused when DBG is not defined; silence those
 * host-compile warnings around the include. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "device/memory/memory.c"
#pragma GCC diagnostic pop

#include "api/ra_glue.c"

enum { KSEG0_BASE = 0x80000000u, KSEG1_BASE = 0xA0000000u };

/* Collect every failed check instead of aborting on the first one. */
static int g_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

static uint8_t pattern_byte(uint32_t offset)
{
    return (uint8_t)(offset ^ (offset >> 8) ^ (offset >> 16));
}

static void check_read(const uint8_t* rdram, uint32_t address, uint32_t num_bytes,
                       uint32_t expected_offset)
{
    uint8_t buffer[4] = { 0 };
    uint32_t num_read = ra_glue_read_memory(address, buffer, num_bytes, NULL);

    CHECK(num_read == num_bytes);
    if (num_read == num_bytes) {
        CHECK(memcmp(buffer, rdram + expected_offset, num_bytes) == 0);
    }
}

static void check_region(const uint8_t* rdram, const rc_memory_region_t* region)
{
    const uint32_t size = region->end_address - region->start_address + 1;
    const uint32_t deltas[] = { 0, 1, size / 2, size - 4, size - 1 };
    size_t i;

    CHECK(region->real_address >= KSEG0_BASE);
    CHECK(region->real_address + (size - 1) < KSEG1_BASE);

    for (i = 0; i < sizeof(deltas) / sizeof(deltas[0]); ++i) {
        const uint32_t address = region->start_address + deltas[i];
        const uint32_t expected_offset =
            region->real_address + deltas[i] - KSEG0_BASE - MM_RDRAM_DRAM;

        check_read(rdram, address, 1, expected_offset);
        if (deltas[i] + 4 <= size) {
            check_read(rdram, address, 4, expected_offset);
        }
    }
}

/* A read never goes past the 16 MB RDRAM area of the memory base (memory.h). */
static void check_rdram_end(uint8_t* rdram)
{
    uint8_t buffer[4] = { 0 };
    uint32_t num_read;

    rdram[RDRAM_16MB_SIZE - 2] = pattern_byte(RDRAM_16MB_SIZE - 2);
    rdram[RDRAM_16MB_SIZE - 1] = pattern_byte(RDRAM_16MB_SIZE - 1);

    num_read = ra_glue_read_memory(RDRAM_16MB_SIZE - 2, buffer, 4, NULL);
    CHECK(num_read <= 2);
    if (num_read <= 2) {
        CHECK(memcmp(buffer, rdram + RDRAM_16MB_SIZE - 2, num_read) == 0);
    }
    CHECK(ra_glue_read_memory(RDRAM_16MB_SIZE, buffer, 1, NULL) == 0);
    CHECK(ra_glue_read_memory(0, buffer, 0, NULL) == 0);
}

static void check_mem_base(void* base, const char* mode)
{
    const rc_memory_regions_t* regions = rc_console_memory_regions(RC_CONSOLE_NINTENDO_64);
    uint8_t* rdram = (uint8_t*)mem_base_u32(base, MM_RDRAM_DRAM);
    uint32_t last_offset;
    uint32_t offset;
    uint32_t i;
    const int failures_before = g_failures;

    CHECK(regions != NULL && regions->num_regions > 0);
    if (regions == NULL || regions->num_regions == 0) {
        return;
    }

    last_offset = regions->region[regions->num_regions - 1].real_address
        + (regions->region[regions->num_regions - 1].end_address
           - regions->region[regions->num_regions - 1].start_address)
        - KSEG0_BASE - MM_RDRAM_DRAM;
    for (offset = 0; offset <= last_offset; ++offset) {
        rdram[offset] = pattern_byte(offset);
    }

    g_mem_base = base;
    for (i = 0; i < regions->num_regions; ++i) {
        check_region(rdram, &regions->region[i]);
    }
    check_rdram_end(rdram);
    g_mem_base = NULL;

    printf("%s mem base: %s\n", mode, g_failures == failures_before ? "ok" : "FAILED");
}

int main(void)
{
    uint8_t buffer[4];
    void* full_base;
    void* compressed_base;

    g_mem_base = NULL;
    CHECK(ra_glue_read_memory(0, buffer, 1, NULL) == 0);

    full_base = init_mem_base();
    CHECK(full_base != NULL && MEM_BASE_MODE(full_base) == 0);
    if (full_base != NULL && MEM_BASE_MODE(full_base) == 0) {
        check_mem_base(full_base, "full");
    }
    if (full_base != NULL) {
        release_mem_base(full_base);
    }

    compressed_base = malloc(MB_MAX_SIZE);
    CHECK(compressed_base != NULL);
    if (compressed_base != NULL) {
        SET_MEM_BASE_MODE(compressed_base);
        check_mem_base(compressed_base, "compressed");
        release_mem_base(compressed_base);
    }

    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    return 0;
}
