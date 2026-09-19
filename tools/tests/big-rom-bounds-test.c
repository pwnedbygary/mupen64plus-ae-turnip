/*
 * Host fixture for cart ROM addressing above the 64MB boundary.
 *
 * Builds a synthetic 96MB ROM image (the size class of B3313 v1.0.2 /
 * issue #3) and checks the production cart_rom.c entry points directly:
 *
 *   - PI DMA ROM -> RDRAM  (cart_rom_dma_write)
 *   - CPU read             (read_cart_rom)
 *   - PI DMA RDRAM -> ROM  (cart_rom_dma_read, WritableROM path)
 *
 * Expectations are derived from the N64 cartridge address map
 * (true ROM offset = cart address - MM_CART_ROM, MM_CART_ROM = 0x10000000)
 * and from the emulator RDRAM byte-swizzle convention (S8).  A ROM of
 * TEST_ROM_SIZE must fit below MM_PIF_MEM and must not overlap the RSP
 * slot of the compressed memory base, which starts at
 * MB_CART_ROM + CART_ROM_MAX_SIZE.
 *
 * The memory base layout and mem_base_u32() are included from the
 * production memory.c so the layout assertions exercise the real code.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "device/cart/cart_rom.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/pi/pi_controller.h"
#include "main/main.h"
#include "osal/files.h"

/* ------------------------------------------------------------------ */
/* Stubs for the production cart_rom.c translation unit.              */
/* ------------------------------------------------------------------ */
m64p_rom_settings ROM_SETTINGS;
m64p_handle g_CoreConfig;

void DebugMessage(int level, const char* message, ...)
{
    (void)level;
    (void)message;
}

int validate_pi_request(struct pi_controller* pi)
{
    (void)pi;
    return 1;
}

void invalidate_r4300_cached_code(struct r4300_core* r4300, uint32_t address, size_t size)
{
    (void)r4300;
    (void)address;
    (void)size;
}

unsigned int add_random_interrupt_time(struct r4300_core* r4300)
{
    (void)r4300;
    return 0;
}

/* WritableROM persistence is not exercised for I/O; make every path lookup
 * fail so the tests never create files. */
const char* ConfigGetParamString(m64p_handle handle, const char* param)
{
    (void)handle;
    (void)param;
    return NULL;
}

const char* ConfigGetUserDataPath(void)
{
    return "/nonexistent-big-rom-host-test";
}

int osal_mkdirp(const char* dirpath, int mode)
{
    (void)dirpath;
    (void)mode;
    return 0;
}

/* Production memory base layout (MB_* enum) and mem_base_u32().
 * The debug-only paths in memory.c leave parameters unused when DBG is
 * not defined; silence those host-compile warnings around the include. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "device/memory/memory.c"
#pragma GCC diagnostic pop

/* ------------------------------------------------------------------ */
/* Test ROM image                                                     */
/* ------------------------------------------------------------------ */
enum { TEST_ROM_SIZE = 0x6000000 }; /* 96MB, B3313 v1.0.2 class */
enum { CANARY = 0xE7 };

/* Collect every failed check instead of aborting on the first one, so a
 * failing-before run identifies each broken entry point. */
static int g_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

/* ROM buffer with 4 canary bytes on either side of the cart image. */
static uint8_t* g_buf;

/* RDRAM byte-swizzle helpers: mirror the emulator convention so that a
 * big-endian 32-bit word survives a round trip through the ^S8 mapping. */
static void swizzle_write(uint8_t* buf, uint32_t addr, uint32_t value)
{
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        buf[(addr + i) ^ S8] = (uint8_t)(value >> (24 - 8 * i));
    }
}

static uint32_t swizzle_read(const uint8_t* buf, uint32_t addr)
{
    uint32_t value = 0;
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        value = (value << 8) | buf[(addr + i) ^ S8];
    }
    return value;
}

static void check_canaries(void)
{
    int i;
    for (i = 0; i < 4; ++i) {
        CHECK(g_buf[i] == CANARY);
        CHECK(g_buf[4 + TEST_ROM_SIZE + i] == CANARY);
    }
}

static uint8_t* rom(void)
{
    return g_buf + 4;
}

/* PI DMA ROM -> RDRAM at the last word of the ROM must resolve to the true
 * offset and stay in bounds. */
static void test_dma_last_word(void)
{
    struct cart_rom cr;
    uint8_t dram[16];

    memset(&cr, 0, sizeof(cr));
    cr.rom = rom();
    cr.rom_size = TEST_ROM_SIZE;

    swizzle_write(rom(), TEST_ROM_SIZE - 4, 0x5A5A5A5A);
    memset(dram, 0, sizeof(dram));

    cart_rom_dma_write(&cr, dram, 0, MM_CART_ROM + TEST_ROM_SIZE - 4, 4);

    CHECK(swizzle_read(dram, 0) == 0x5A5A5A5A);
}

/* CPU read of the last word below the old 64MB boundary must still resolve
 * to the same ROM offset after widening the mask. */
static void test_cpu_below_64mb_boundary(void)
{
    struct pi_controller pi;
    struct cart_rom cr;
    uint32_t value;

    memset(&pi, 0, sizeof(pi));
    memset(&cr, 0, sizeof(cr));
    cr.rom = rom();
    cr.rom_size = TEST_ROM_SIZE;
    cr.pi = &pi;

    swizzle_write(rom(), 0x03FFFFFC, 0x6B6B6B6B);

    read_cart_rom(&cr, MM_CART_ROM + 0x03FFFFFC, &value);

    CHECK(value == 0x6B6B6B6B);
}

/* A 96MB ROM must fit in the address window below PIF and in the
 * compressed memory base cart slot. */
static void test_memory_layout(void)
{
    void* base;
    void* compressed_base;
    uint8_t* last;
    uint32_t last_cart_addr = MM_CART_ROM + TEST_ROM_SIZE - 1;

    CHECK((size_t)CART_ROM_MAX_SIZE >= (size_t)TEST_ROM_SIZE);
    CHECK((size_t)(MB_RSP_MEM - MB_CART_ROM) >= (size_t)TEST_ROM_SIZE);
    CHECK((size_t)last_cart_addr < (size_t)MM_PIF_MEM);

    base = malloc(MB_MAX_SIZE);
    CHECK(base != NULL);
    if (base == NULL) {
        return;
    }

    /* Compressed mem base mode is flagged by LSB = 1. */
    compressed_base = (void*)((uintptr_t)base | 1u);
    last = (uint8_t*)mem_base_u32(compressed_base, last_cart_addr);
    CHECK(last == (uint8_t*)base + MB_CART_ROM + TEST_ROM_SIZE - 1);
    CHECK((size_t)(last - (uint8_t*)base) < (size_t)MB_RSP_MEM);

    free(base);
}

/* PI DMA ROM -> RDRAM at a cart address above 64MB must copy the true
 * ROM offset, not a 64MB-wrapped one. */
static void test_dma_rom_to_rdram(void)
{
    struct cart_rom cr;
    uint8_t dram[16];

    memset(&cr, 0, sizeof(cr));
    cr.rom = rom();
    cr.rom_size = TEST_ROM_SIZE;

    swizzle_write(rom(), 0x0000000, 0x11111111);        /* wrapped offset */
    swizzle_write(rom(), 0x04000000, 0x22222222);       /* true offset    */
    memset(dram, 0, sizeof(dram));

    cart_rom_dma_write(&cr, dram, 0, MM_CART_ROM + 0x04000000, 4);

    CHECK(swizzle_read(dram, 0) == 0x22222222);
}

/* CPU reads use rom_address(); a read above 64MB must return the true
 * ROM offset. */
static void test_cpu_read(void)
{
    struct pi_controller pi;
    struct cart_rom cr;
    uint32_t value;

    memset(&pi, 0, sizeof(pi));
    memset(&cr, 0, sizeof(cr));
    cr.rom = rom();
    cr.rom_size = TEST_ROM_SIZE;
    cr.pi = &pi;

    swizzle_write(rom(), 0x0000000, 0xAAAAAAAA);        /* wrapped offset */
    swizzle_write(rom(), 0x04000000, 0xBBBBBBBB);       /* true offset    */

    read_cart_rom(&cr, MM_CART_ROM + 0x04000000, &value);

    CHECK(value == 0xBBBBBBBB);
    CHECK(value != 0xAAAAAAAA);
}

/* WritableROM PI DMA RDRAM -> ROM above 64MB must write the true ROM
 * offset and leave the wrapped offset untouched. */
static void test_writable_dma_into_rom(void)
{
    struct cart_rom cr;
    uint8_t dram[16];

    memset(&cr, 0, sizeof(cr));
    cr.rom = rom();
    cr.rom_size = TEST_ROM_SIZE;

    swizzle_write(rom(), 0x0000000, 0xCCCCCCCC);        /* wrapped offset */
    swizzle_write(rom(), 0x04000000, 0xDDDDDDDD);       /* true offset    */
    swizzle_write(dram, 0, 0x12345678);

    ROM_SETTINGS.writablecartrom = 1;
    cart_rom_dma_read(&cr, dram, 0, MM_CART_ROM + 0x04000000, 4);
    ROM_SETTINGS.writablecartrom = 0;

    CHECK(swizzle_read(rom(), 0x04000000) == 0x12345678);
    CHECK(swizzle_read(rom(), 0x0000000) == 0xCCCCCCCC);
}

int main(void)
{
    g_buf = malloc((size_t)TEST_ROM_SIZE + 8);
    CHECK(g_buf != NULL);
    memset(g_buf, CANARY, (size_t)TEST_ROM_SIZE + 8);
    memset(rom(), 0, TEST_ROM_SIZE);
    memset(&ROM_SETTINGS, 0, sizeof(ROM_SETTINGS));

    test_dma_rom_to_rdram();
    test_cpu_read();
    test_writable_dma_into_rom();
    test_dma_last_word();
    test_cpu_below_64mb_boundary();
    test_memory_layout();
    check_canaries();

    free(g_buf);
    if (g_failures != 0) {
        fprintf(stderr, "%d big-ROM check(s) failed\n", g_failures);
        return 1;
    }
    return 0;
}
