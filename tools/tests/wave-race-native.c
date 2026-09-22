/* Compile the ACTUAL parser, inheritance resolver and open_rom lookup.
 * Platform memory and MD5 are test seams: these are synthetic 64-byte headers,
 * not ROMs, and the selected digest is injected (no hash/content verification).
 */
#include <assert.h>
#include <stdarg.h>
#include "../../mupen64plus-core/upstream/src/main/rom.c"
#include "wave-race-policy.h"

static const char *database;
static md5_byte_t selected_digest[16];
static uint32_t cart[64];
void *g_mem_base;
int g_RomWordsLittleEndian;

const char *ConfigGetSharedDataFilepath(const char *name)
{
    assert(strcmp(name, "mupen64plus.ini") == 0);
    return database;
}
FILE *osal_file_open(const char *name, const char *mode) { return fopen(name, mode); }
void DebugMessage(int level, const char *format, ...) { (void)level; (void)format; }
uint32_t *mem_base_u32(void *base, uint32_t address)
{
    (void)base;
    assert(address == MM_CART_ROM);
    return cart;
}
void md5_init(md5_state_t *state) { (void)state; }
void md5_append(md5_state_t *state, const md5_byte_t *data, int length)
{ (void)state; (void)data; (void)length; }
void md5_finish(md5_state_t *state, md5_byte_t digest[16])
{ (void)state; memcpy(digest, selected_digest, 16); }

static void select_md5(const char *text)
{
    for (int i = 0; i < 16; i++) {
        unsigned int byte;
        assert(sscanf(text + 2*i, "%2x", &byte) == 1);
        selected_digest[i] = byte;
    }
}
static void load(const char *md5, uint32_t crc1, uint32_t crc2, unsigned cpo)
{
    unsigned char header[64] = {0x80, 0x37, 0x12, 0x40};
    select_md5(md5);
    for (int i = 0; i < 4; i++) {
        header[16+i] = crc1 >> (24-8*i);
        header[20+i] = crc2 >> (24-8*i);
    }
    memcpy(header + 32, "HOST TEST", 9);
    assert(open_rom(header, sizeof(header)) == M64ERR_SUCCESS);
    assert(ROM_SETTINGS.countperop == cpo);
    assert(strcmp(ROM_SETTINGS.MD5, md5) == 0);
}
int main(int argc, char **argv)
{
    const char *translated = "EFDE606C824DAACF715928B914AC26E0";
    const char *original = "FF67DF97476C210D158779AE6142F239";
    const char *unknown = "00000000000000000000000000000000";
    assert(argc == 2);
    database = argv[1];
    romdatabase_open();
    select_md5(original);
    romdatabase_entry *base = ini_search_by_md5(selected_digest);
    assert(base && base->countperop == 3);
    select_md5(translated);
    romdatabase_entry *entry = ini_search_by_md5(selected_digest);
    assert(entry && entry->countperop == 3);
    assert(strcmp(entry->goodname, "Wave Race 64 - Shindou Edition (J) (English translation)") == 0);
    assert(entry->crc1 == 0x57AF88CE && entry->crc2 == 0xEDE723DA);
    assert(entry->savetype == base->savetype);
    assert(entry->players == base->players && entry->rumble == base->rumble);
    assert(entry->status == base->status && entry->mempak == base->mempak);
    load(translated, 0, 0, 3); /* MD5 works independently of header CRC. */
    assert(ROM_SETTINGS.savetype == base->savetype);
    assert(production_cpo_fallback(0) == 3);
    for (unsigned override = 1; override <= 5; override++)
        assert(production_cpo_fallback(override) == override);
    load(original, 0, 0, 3);
    load(unknown, 0, 0, 2);
    assert(production_cpo_fallback(0) == 2);
    /* Existing unique-CRC fallback, NOT strictly MD5-only recognition. */
    load(unknown, 0x57AF88CE, 0xEDE723DA, 3);
    assert(strcmp(ROM_SETTINGS.goodname, entry->goodname) == 0);
    romdatabase_close();
    puts("PASS native production DB parser/inheritance/open_rom; unknown default 2 and CRC fallback");
    puts("PASS extracted production main.c CountPerOp fallback statement: auto 3/2; explicit 1..5 unchanged (not full main/device execution)");
    return 0;
}