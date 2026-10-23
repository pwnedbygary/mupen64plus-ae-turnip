/* Host check that every Controller Pak write reaches the save file.
 *
 * The core keeps the four Controller Paks in one file and gives each player's
 * pak a 32 KB part of it (main_run in main.c). A pak write copies the bytes
 * into that part and saves their range (write_mempak in mempak.c). The first
 * save of a session writes the whole file, so a save that put the range at the
 * pak's own address instead of its part's place in the file lost every later
 * write to the paks of players 2 to 4.
 *
 * The test sets up the paks as main_run does, writes to each one after the
 * session's first save, compares the file with the bytes it should hold, and
 * reads the blocks back as the next session would.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/callbacks.h"
#include "backends/api/storage_backend.h"
#include "backends/file_storage.h"
#include "device/controllers/game_controller.h"
#include "device/controllers/paks/mempak.h"
#include "main/util.h"

enum { PAKS = 4, BLOCK = 32, FILE_SIZE = PAKS * MEMPAK_SIZE };

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

struct pak_write
{
    int player; /* 0 is player 1 */
    uint16_t address;
    uint8_t value;
};

/* The first write is the session's first save, which writes the whole file;
 * every later one saves only its range. No two blocks overlap. */
static const struct pak_write g_writes[] = {
    { 0, 0x0100, 0x11 },
    { 1, 0x0100, 0x21 },
    { 2, 0x0100, 0x31 },
    { 3, 0x0100, 0x41 },
    { 1, 0x7fe0, 0x22 },
    { 2, 0x7fe0, 0x32 },
    { 3, 0x7fe0, 0x42 },
    { 0, 0x4000, 0x12 },
};

struct session
{
    struct file_storage file;
    struct file_storage parts[PAKS];
    struct mempak paks[PAKS];
};

/* The save file before the session. Its bytes change from one offset to the
 * next, so a block of one value that is missing or misplaced shows. */
static uint8_t old_byte(size_t offset)
{
    return (uint8_t)(offset * 7 + offset / 256);
}

static int open_session(struct session* s, const char* path)
{
    int i;

    memset(s, 0, sizeof(*s));
    if (open_file_storage(&s->file, FILE_SIZE, strdup(path)) != file_ok) {
        return 0;
    }
    for (i = 0; i < PAKS; ++i) {
        s->parts[i].data = s->file.data + i * MEMPAK_SIZE;
        s->parts[i].size = MEMPAK_SIZE;
        s->parts[i].offset = i * MEMPAK_SIZE;
        s->parts[i].filename = (void*)&s->file;
        init_mempak(&s->paks[i], &s->parts[i], &g_isubfile_storage);
    }
    return 1;
}

int main(void)
{
    const char* tmp = getenv("TMPDIR");
    uint8_t* expected = malloc(FILE_SIZE);
    uint8_t* actual = malloc(FILE_SIZE);
    char path[1024];
    struct session s;
    size_t i;
    int player, read_back = 1;
    FILE* f;

    snprintf(path, sizeof(path), "%s/paks.mpk", tmp != NULL ? tmp : "/tmp");
    for (i = 0; i < FILE_SIZE; ++i) {
        expected[i] = old_byte(i);
    }
    f = fopen(path, "wb");
    CHECK(f != NULL && fwrite(expected, 1, FILE_SIZE, f) == FILE_SIZE);
    if (f != NULL) {
        fclose(f);
    }

    CHECK(open_session(&s, path));
    for (i = 0; i < sizeof(g_writes) / sizeof(g_writes[0]); ++i) {
        const struct pak_write* w = &g_writes[i];
        uint8_t block[BLOCK];

        memset(block, w->value, sizeof(block));
        g_imempak.write(&s.paks[w->player], w->address, block, sizeof(block));
        memset(expected + w->player * MEMPAK_SIZE + w->address, w->value, BLOCK);
    }
    close_file_storage(&s.file);

    f = fopen(path, "rb");
    CHECK(f != NULL && fread(actual, 1, FILE_SIZE, f) == FILE_SIZE);
    if (f != NULL) {
        fclose(f);
    }
    for (player = 0; player < PAKS; ++player) {
        size_t start = (size_t)player * MEMPAK_SIZE;
        int same = memcmp(actual + start, expected + start, MEMPAK_SIZE) == 0;

        CHECK(same);
        printf("player %d's part of the file: %s\n", player + 1, same ? "ok" : "FAILED");
    }

    CHECK(open_session(&s, path));
    for (i = 0; i < sizeof(g_writes) / sizeof(g_writes[0]); ++i) {
        const struct pak_write* w = &g_writes[i];
        uint8_t block[BLOCK];
        size_t k;

        g_imempak.read(&s.paks[w->player], w->address, block, sizeof(block));
        for (k = 0; k < sizeof(block); ++k) {
            if (block[k] != w->value) {
                fprintf(stderr, "FAIL: player %d's block at 0x%04x reads back 0x%02x, not 0x%02x\n",
                        w->player + 1, w->address, block[k], w->value);
                read_back = 0;
                break;
            }
        }
    }
    close_file_storage(&s.file);
    CHECK(read_back);
    printf("blocks read back in the next session: %s\n", read_back ? "ok" : "FAILED");

    remove(path);
    free(actual);
    free(expected);
    return g_failures == 0 ? 0 : 1;
}
