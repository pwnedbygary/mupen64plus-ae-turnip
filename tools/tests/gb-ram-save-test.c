/* Host check that a Game Boy save keeps what follows its cartridge RAM.
 *
 * Other Game Boy emulators append data to the cartridge RAM in a save file,
 * such as the clock of an MBC3 cartridge. The core opens a Transfer Pak's save
 * at the RAM size of the game's header and marks it to keep what follows
 * (init_gb_ram in main.c). The first save of a session writes all of the RAM,
 * and it used to write the file at the RAM's size, which cut off the rest.
 *
 * The test opens saves as init_gb_ram does, from a struct of leftover bytes,
 * and writes to the RAM. A marked save keeps the 48 bytes after its RAM, and
 * one with no file yet is created at the RAM's size by its first save. A save
 * that is not marked, like those of the other save types, is still written at
 * its size, which needs open_file_storage to clear the mark.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/callbacks.h"
#include "backends/api/storage_backend.h"
#include "backends/file_storage.h"
#include "main/util.h"

enum { RAM_SIZE = 0x8000, TRAILER = 48, FILE_SIZE = RAM_SIZE + TRAILER };

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

struct ram_write
{
    size_t offset;
    uint8_t value;
};

/* The first write is the session's first save. */
static const struct ram_write g_writes[] = {
    { 0x0100, 0xa5 },
    { 0x7fff, 0x5a },
};

/* The save file before the session. Its bytes change from one offset to the
 * next, so a byte that is lost or misplaced shows. */
static uint8_t old_byte(size_t offset)
{
    return (uint8_t)(offset * 7 + offset / 256);
}

static void write_old_file(const char* path, uint8_t* expected)
{
    size_t i;
    FILE* f;

    for (i = 0; i < FILE_SIZE; ++i) {
        expected[i] = old_byte(i);
    }
    f = fopen(path, "wb");
    CHECK(f != NULL && fwrite(expected, 1, FILE_SIZE, f) == FILE_SIZE);
    if (f != NULL) {
        fclose(f);
    }
}

/* Opens the save as init_gb_ram does, marks it if asked, and makes the first
 * count writes of g_writes. */
static void write_ram(const char* path, int mark, size_t count, uint8_t* expected)
{
    struct file_storage ram;
    size_t i;

    memset(&ram, 0xff, sizeof(ram));
    if (open_file_storage(&ram, RAM_SIZE, strdup(path)) == file_open_error) {
        memset(ram.data, 0, RAM_SIZE);
    }
    if (mark) {
        ram.keep_trailing_data = 1;
    }
    for (i = 0; i < count; ++i) {
        ram.data[g_writes[i].offset] = g_writes[i].value;
        g_ifile_storage.save(&ram, g_writes[i].offset, 1);
        expected[g_writes[i].offset] = g_writes[i].value;
    }
    close_file_storage(&ram);
}

/* Whether the file is exactly the first size bytes of expected. */
static int file_holds(const char* path, const uint8_t* expected, size_t size)
{
    uint8_t* actual = malloc(FILE_SIZE + 1);
    FILE* f = fopen(path, "rb");
    size_t read = 0;
    int same;

    if (f != NULL) {
        read = fread(actual, 1, FILE_SIZE + 1, f);
        fclose(f);
    }
    same = read == size && memcmp(actual, expected, size) == 0;
    free(actual);
    return same;
}

int main(void)
{
    const char* tmp = getenv("TMPDIR");
    uint8_t* expected = malloc(FILE_SIZE);
    char path[1024];
    int same;

    snprintf(path, sizeof(path), "%s/game.sav", tmp != NULL ? tmp : "/tmp");

    write_old_file(path, expected);
    write_ram(path, 1, 2, expected);
    same = file_holds(path, expected, FILE_SIZE);
    CHECK(same);
    printf("marked save keeps the 48 bytes after its RAM: %s\n", same ? "ok" : "FAILED");

    write_old_file(path, expected);
    write_ram(path, 0, 2, expected);
    same = file_holds(path, expected, RAM_SIZE);
    CHECK(same);
    printf("unmarked save is written at its size: %s\n", same ? "ok" : "FAILED");

    remove(path);
    memset(expected, 0, FILE_SIZE);
    write_ram(path, 1, 1, expected);
    same = file_holds(path, expected, RAM_SIZE);
    CHECK(same);
    printf("marked save with no file is created at its RAM size: %s\n", same ? "ok" : "FAILED");

    remove(path);
    free(expected);
    return g_failures == 0 ? 0 : 1;
}
