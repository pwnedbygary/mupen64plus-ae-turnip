/* Host check that cartridge saves stay inside their chips and RDRAM.
 *
 * The core keeps a cartridge's SRAM in a 32 KB buffer but masks SRAM
 * addresses with 0xffff, and keeps FlashRAM in a 128 KB buffer but takes the
 * page of a program or erase command from 16 bits the game writes. CPU
 * accesses, commands and DMAs reached past both buffers and past RDRAM, and
 * a DMA into SRAM asked the storage to save more than it copied. The
 * IS-Viewer moved the rest of its output buffer to the front with memcpy,
 * whose source and destination overlap.
 *
 * The test builds the production sram.c, flashram.c and is_viewer.c. Its
 * stand-in storage holds the chip followed by a guard of canary bytes and
 * records each save, and its RDRAM buffer has a guard after the RDRAM size
 * the device reports. Expectations come from the sizes alone: SRAM is
 * 32 KB, FlashRAM is 1024 pages of 128 bytes in sectors of 128 pages, and
 * nothing may be read from or written to a guard, or saved past the chip.
 * A copy that reads past the chip or RDRAM brings canary bytes along. Built
 * with AddressSanitizer, as the script does where the compiler has it, an
 * overlapping memcpy in the IS-Viewer also fails the run.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/callbacks.h"
#include "backends/api/storage_backend.h"
#include "device/cart/flashram.h"
#include "device/cart/is_viewer.h"
#include "device/cart/sram.h"
#include "device/rdram/rdram.h"

static int g_failures;
static char g_message[256];

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            g_failures++;                                                   \
        }                                                                   \
    } while (0)

void DebugMessage(int level, const char *message, ...)
{
    va_list args;
    (void)level;
    va_start(args, message);
    vsnprintf(g_message, sizeof(g_message), message, args);
    va_end(args);
}

enum { GUARD_SIZE = 0x8000, CANARY = 0xe7, CHIP_FILL = 0x11, DRAM_FILL = 0x22 };
enum { TEST_DRAM_SIZE = 0x1000 };

/* The chip, followed by a guard, and the saves asked of it. */
struct test_storage
{
    uint8_t *data;
    size_t size;
    unsigned int saves;
    size_t last_start;
    size_t last_size;
    int saved_outside;
};

static uint8_t *storage_data(const void *storage)
{
    return ((const struct test_storage *)storage)->data;
}

static size_t storage_size(const void *storage)
{
    return ((const struct test_storage *)storage)->size;
}

static void storage_save(void *storage, size_t start, size_t size)
{
    struct test_storage *s = (struct test_storage *)storage;

    s->saves++;
    s->last_start = start;
    s->last_size = size;
    if (start > s->size || size > s->size - start)
        s->saved_outside = 1;
}

static const struct storage_backend_interface g_istorage = {
    storage_data, storage_size, storage_save
};

static struct test_storage g_storage;
static struct rdram g_rdram;
static uint8_t *g_dram;

static void reset_storage(size_t chip_size)
{
    free(g_storage.data);
    memset(&g_storage, 0, sizeof(g_storage));
    g_storage.size = chip_size;
    g_storage.data = malloc(chip_size + GUARD_SIZE);
    if (g_storage.data == NULL) {
        perror("malloc");
        exit(1);
    }
    memset(g_storage.data, CHIP_FILL, chip_size);
    memset(g_storage.data + chip_size, CANARY, GUARD_SIZE);
}

static void reset_dram(void)
{
    memset(g_dram, DRAM_FILL, TEST_DRAM_SIZE);
    memset(g_dram + TEST_DRAM_SIZE, CANARY, GUARD_SIZE);
}

static int all_bytes(const uint8_t *bytes, size_t count, uint8_t value)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (bytes[i] != value)
            return 0;
    }
    return 1;
}

static int storage_guard_intact(void)
{
    return all_bytes(g_storage.data + g_storage.size, GUARD_SIZE, CANARY);
}

static int dram_guard_intact(void)
{
    return all_bytes(g_dram + TEST_DRAM_SIZE, GUARD_SIZE, CANARY);
}

static void report(const char *run, int failures_before)
{
    printf("%s: %s\n", run, g_failures == failures_before ? "ok" : "FAILED");
    fflush(stdout);
}

static void init_test_sram(struct sram *sram)
{
    reset_storage(SRAM_SIZE);
    reset_dram();
    init_sram(sram, &g_storage, &g_istorage, &g_rdram);
}

static void check_sram_cpu_access(void)
{
    struct sram sram;
    uint32_t value = 0;
    int before = g_failures;

    init_test_sram(&sram);
    read_sram(&sram, 0x08007ffc, &value);
    CHECK(value == 0x11111111);
    write_sram(&sram, 0x08007ffc, 0x12345678, 0xffffffff);
    CHECK(g_storage.saves == 1 && g_storage.last_start == 0x7ffc && g_storage.last_size == 4);

    value = 0xdeadbeef;
    read_sram(&sram, 0x08008000, &value);
    CHECK(value == 0);
    read_sram(&sram, 0x0800fffc, &value);
    CHECK(value == 0);
    write_sram(&sram, 0x08008000, 0x12345678, 0xffffffff);
    write_sram(&sram, 0x0800fffc, 0x12345678, 0xffffffff);
    CHECK(g_storage.saves == 1);
    CHECK(!g_storage.saved_outside);
    CHECK(storage_guard_intact());
    report("SRAM: a CPU access past 32 KB reads 0 and writes nothing", before);
}

static void check_sram_dma_into_sram(void)
{
    struct sram sram;
    int before = g_failures;

    init_test_sram(&sram);
    sram_dma_read(&sram, g_dram, 0, 0x08007ff8, 16);
    CHECK(all_bytes(g_storage.data + 0x7ff8, 8, DRAM_FILL));
    CHECK(g_storage.saves == 1 && g_storage.last_start == 0x7ff8 && g_storage.last_size == 8);
    CHECK(!g_storage.saved_outside);
    CHECK(storage_guard_intact());
    report("SRAM: a DMA into SRAM stops at its end and saves what it copied", before);

    before = g_failures;
    init_test_sram(&sram);
    sram_dma_read(&sram, g_dram, TEST_DRAM_SIZE - 8, 0x08000000, 16);
    CHECK(all_bytes(g_storage.data, 8, DRAM_FILL));
    CHECK(all_bytes(g_storage.data + 8, 8, CHIP_FILL));
    CHECK(g_storage.saves == 1 && g_storage.last_start == 0 && g_storage.last_size == 8);
    report("SRAM: a DMA into SRAM stops at the end of RDRAM", before);

    before = g_failures;
    init_test_sram(&sram);
    sram_dma_read(&sram, g_dram, 0, 0x08008000, 16);
    CHECK(g_storage.saves == 0 && !g_storage.saved_outside);
    CHECK(storage_guard_intact());
    report("SRAM: a DMA into SRAM past 32 KB copies and saves nothing", before);

    before = g_failures;
    init_test_sram(&sram);
    sram_dma_read(&sram, g_dram, TEST_DRAM_SIZE, 0x08000000, 16);
    CHECK(all_bytes(g_storage.data, SRAM_SIZE, CHIP_FILL));
    CHECK(g_storage.saves == 0);
    report("SRAM: a DMA into SRAM from the end of RDRAM copies and saves nothing", before);
}

static void check_sram_dma_from_sram(void)
{
    struct sram sram;
    int before = g_failures;

    init_test_sram(&sram);
    sram_dma_write(&sram, g_dram, 0, 0x08007ff8, 16);
    CHECK(all_bytes(g_dram, 8, CHIP_FILL));
    CHECK(all_bytes(g_dram + 8, 8, DRAM_FILL));
    report("SRAM: a DMA from SRAM stops at its end", before);

    before = g_failures;
    init_test_sram(&sram);
    sram_dma_write(&sram, g_dram, TEST_DRAM_SIZE - 8, 0x08000000, 16);
    CHECK(all_bytes(g_dram + TEST_DRAM_SIZE - 8, 8, CHIP_FILL));
    CHECK(dram_guard_intact());
    report("SRAM: a DMA from SRAM stops at the end of RDRAM", before);
}

static void flashram_command(struct flashram *flashram, uint32_t command)
{
    write_flashram(flashram, 0x08010000, command, 0xffffffff);
}

static void init_test_flashram(struct flashram *flashram, uint32_t flashram_id)
{
    reset_storage(FLASHRAM_SIZE);
    reset_dram();
    memset(flashram, 0, sizeof(*flashram));
    init_flashram(flashram, flashram_id, &g_storage, &g_istorage, &g_rdram);
    poweron_flashram(flashram);
}

/* Fills the page buffer from RDRAM, then programs the page. */
static void program_page(struct flashram *flashram, uint32_t page)
{
    flashram_command(flashram, 0xb4000000);
    flashram_dma_read(flashram, g_dram, 0, 0x08000000, 128);
    flashram_command(flashram, 0xa5000000 | page);
}

static void check_flashram_program(void)
{
    struct flashram flashram;
    int before = g_failures;

    init_test_flashram(&flashram, MX29L1101_ID);
    program_page(&flashram, 0x3ff);
    CHECK(all_bytes(g_storage.data + 0x1ff80, 128, DRAM_FILL));
    CHECK(g_storage.saves == 1 && g_storage.last_start == 0x1ff80 && g_storage.last_size == 128);
    program_page(&flashram, 0x400);
    CHECK(g_storage.saves == 1);
    CHECK(!g_storage.saved_outside);
    CHECK(storage_guard_intact());
    report("FlashRAM: a page past the chip's 1024 is not programmed or saved", before);
}

static void check_flashram_sector_erase(void)
{
    struct flashram flashram;
    int before = g_failures;

    init_test_flashram(&flashram, MX29L1101_ID);
    flashram_command(&flashram, 0x4b000380);
    flashram_command(&flashram, 0x78000000);
    CHECK(all_bytes(g_storage.data + 0x1c000, 0x4000, 0xff));
    CHECK(all_bytes(g_storage.data, 0x1c000, CHIP_FILL));
    CHECK(g_storage.saves == 1 && g_storage.last_start == 0x1c000 && g_storage.last_size == 0x4000);
    flashram_command(&flashram, 0x4b000400);
    flashram_command(&flashram, 0x78000000);
    CHECK(g_storage.saves == 1);
    CHECK(!g_storage.saved_outside);
    CHECK(storage_guard_intact());
    report("FlashRAM: a sector past the chip's 8 is not erased or saved", before);
}

static void check_flashram_silicon_id(void)
{
    struct flashram flashram;
    uint32_t words[2];
    int before = g_failures;

    init_test_flashram(&flashram, MX29L1101_ID);
    flashram_command(&flashram, 0xe1000000);
    flashram_dma_write(&flashram, g_dram, TEST_DRAM_SIZE - 8, 0x08000000, 8);
    memcpy(words, g_dram + TEST_DRAM_SIZE - 8, sizeof(words));
    CHECK(words[0] == FLASHRAM_TYPE_ID && words[1] == MX29L1101_ID);
    reset_dram();
    flashram_dma_write(&flashram, g_dram, TEST_DRAM_SIZE - 4, 0x08000000, 8);
    CHECK(all_bytes(g_dram + TEST_DRAM_SIZE - 4, 4, DRAM_FILL));
    CHECK(dram_guard_intact());
    report("FlashRAM: the silicon ID is not written past the end of RDRAM", before);
}

static void check_flashram_read_array(void)
{
    struct flashram flashram;
    int before = g_failures;

    /* This chip's array DMA doubles the cartridge offset, so 0xfffc is the
     * chip's last 8 bytes. */
    init_test_flashram(&flashram, MX29L1100_ID);
    flashram_command(&flashram, 0xf0000000);
    flashram_dma_write(&flashram, g_dram, 0, 0x0800fffc, 16);
    CHECK(all_bytes(g_dram, 8, CHIP_FILL));
    CHECK(all_bytes(g_dram + 8, 8, DRAM_FILL));
    report("FlashRAM: an array DMA stops at the end of the chip", before);
}

static void check_flashram_page_buffer(void)
{
    struct flashram flashram;
    int before = g_failures;

    init_test_flashram(&flashram, MX29L1101_ID);
    flashram_command(&flashram, 0xb4000000);
    flashram_dma_read(&flashram, g_dram, TEST_DRAM_SIZE - 64, 0x08000000, 128);
    CHECK(all_bytes(flashram.page_buf, 64, DRAM_FILL));
    CHECK(all_bytes(flashram.page_buf + 64, 64, 0xff));
    report("FlashRAM: a page-buffer DMA stops at the end of RDRAM", before);
}

static void check_is_viewer(void)
{
    static struct is_viewer is_viewer;
    static const char text[] = "first line\nrest";
    int before = g_failures;

    memset(&is_viewer, 0, sizeof(is_viewer));
    memcpy(&is_viewer.data[0x20], text, sizeof(text) - 1);
    g_message[0] = '\0';
    write_is_viewer(&is_viewer, 0xb3ff0014, sizeof(text) - 1, 0xffffffff);
    CHECK(strcmp(g_message, "IS64: first line") == 0);
    CHECK(is_viewer.buffer_pos == 4);
    CHECK(memcmp(is_viewer.output_buffer, "rest", 4) == 0);
    report("IS-Viewer: a logged line leaves the rest at the front", before);
}

int main(void)
{
    g_dram = malloc(TEST_DRAM_SIZE + GUARD_SIZE);
    if (g_dram == NULL) {
        perror("malloc");
        return 1;
    }
    g_rdram.dram_size = TEST_DRAM_SIZE;

    check_sram_cpu_access();
    check_sram_dma_into_sram();
    check_sram_dma_from_sram();
    check_flashram_program();
    check_flashram_sector_erase();
    check_flashram_silicon_id();
    check_flashram_read_array();
    check_flashram_page_buffer();
    check_is_viewer();

    free(g_storage.data);
    free(g_dram);
    return g_failures != 0;
}
