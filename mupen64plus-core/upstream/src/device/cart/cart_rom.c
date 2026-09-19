/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cart_rom.c                                              *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "cart_rom.h"

#define M64P_CORE_PROTOTYPES 1
#include "api/callbacks.h"
#include "api/m64p_config.h"
#include "api/m64p_types.h"

#include "device/memory/memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/pi/pi_controller.h"
#include "main/main.h"
#include "main/rom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osal/files.h"

/* ---- WritableROM persistence (per-game, ROM DB "WritableROM=True") ----
 * Games that keep a save area inside cart ROM space (e.g. N64DD hacks baked
 * into an extended .z64) PI-DMA-write into the ROM window. When enabled,
 * those writes are applied to the ROM buffer and mirrored to
 * <SaveSRAMPath>/<goodname>.cart_ram, with an .idx sidecar recording
 * {addr,len} ranges so only ever-written regions are restored next boot.
 * File offset == cart address in the data file.
 */
static FILE* l_writable_rom_file = NULL;
static FILE* l_writable_idx_file = NULL;

static int writable_cartrom_get_path(char* path, size_t size)
{
    extern m64p_rom_settings ROM_SETTINGS;
    if (!ROM_SETTINGS.writablecartrom)
        return 0;
    const char* configpath = ConfigGetParamString(g_CoreConfig, "SaveSRAMPath");
    char savepath[1024];
    if (!configpath || strlen(configpath)==0) {
        snprintf(savepath, sizeof(savepath), "%ssave%c", ConfigGetUserDataPath(), OSAL_DIR_SEPARATORS[0]);
    } else {
        snprintf(savepath, sizeof(savepath), "%s%c", configpath, OSAL_DIR_SEPARATORS[0]);
    }
    osal_mkdirp(savepath, 0700);
    snprintf(path, size, "%s%s.cart_ram", savepath, ROM_SETTINGS.goodname);
    return 1;
}

static void writable_cartrom_append_idx(const char* path, uint32_t cart_addr, uint32_t length)
{
    if (!l_writable_idx_file) {
        char idxpath[1032];
        snprintf(idxpath, sizeof(idxpath), "%s.idx", path);
        l_writable_idx_file = fopen(idxpath, "ab");
        if (!l_writable_idx_file) return;
    }
    uint32_t rec[2] = { cart_addr, length };
    fwrite(rec, 1, sizeof(rec), l_writable_idx_file);
}

// Mirror raw ROM buffer bytes for [cart_addr, cart_addr+length) into the
// .cart_ram data file at offset == cart_addr, and record the range in .idx.
static void writable_cartrom_persist_range(uint32_t cart_addr, uint32_t length, uint8_t* rom, size_t rom_size)
{
    char path[1024];
    if (!writable_cartrom_get_path(path, sizeof(path))) return;
    if (cart_addr >= rom_size) return;
    if (cart_addr + length > rom_size) length = (uint32_t)(rom_size - cart_addr);
    if (length == 0) return;

    if (!l_writable_rom_file) {
        l_writable_rom_file = fopen(path, "r+b");
        if (!l_writable_rom_file) l_writable_rom_file = fopen(path, "wb");
        if (!l_writable_rom_file) return;
    }
    fseek(l_writable_rom_file, cart_addr, SEEK_SET);
    fwrite(rom + cart_addr, 1, length, l_writable_rom_file);

    writable_cartrom_append_idx(path, cart_addr, length);
}

static int writable_cartrom_rec_cmp(const void* a, const void* b)
{
    uint32_t aa = ((const uint32_t*)a)[0];
    uint32_t bb = ((const uint32_t*)b)[0];
    return (aa < bb) ? -1 : (aa > bb) ? 1 : 0;
}

// Restore only the ranges recorded in .idx (never touches unwritten ROM regions),
// then self-compact the .idx: sort by address and merge overlapping or exactly
// touching ranges (ranges separated by a gap must stay separate - the data file
// contains zeros in gaps).
static void writable_cartrom_load_persisted(uint8_t* rom, size_t rom_size)
{
    char path[1024], idxpath[1032];
    if (!writable_cartrom_get_path(path, sizeof(path))) return;
    snprintf(idxpath, sizeof(idxpath), "%s.idx", path);
    FILE* fi = fopen(idxpath, "rb");
    if (!fi) return;
    FILE* f = fopen(path, "rb");
    if (!f) { fclose(fi); return; }

    uint32_t (*recs)[2] = NULL;
    size_t count = 0, cap = 0;
    for (;;) {
        uint32_t rec[2];
        if (fread(rec, 1, sizeof(rec), fi) != sizeof(rec)) break;
        uint32_t addr = rec[0], len = rec[1];
        if (addr >= rom_size) continue;
        if ((uint64_t)addr + len > rom_size) len = (uint32_t)(rom_size - addr);
        if (len == 0) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 64;
            uint32_t (*tmp)[2] = (uint32_t (*)[2])realloc(recs, cap * sizeof(*recs));
            if (!tmp) break;
            recs = tmp;
        }
        recs[count][0] = addr;
        recs[count][1] = len;
        ++count;
    }
    fclose(fi);

    if (count == 0) { free(recs); fclose(f); return; }
    qsort(recs, count, sizeof(*recs), writable_cartrom_rec_cmp);

    /* merge + apply */
    size_t w = 0;
    for (size_t i = 0; i < count; ) {
        uint32_t addr = recs[i][0];
        uint32_t len  = recs[i][1];
        size_t j = i + 1;
        while (j < count && recs[j][0] <= (uint64_t)addr + len) {
            uint32_t end = recs[j][0] + recs[j][1];
            if (end > addr + len) len = end - addr;
            ++j;
        }
        if (addr + len > rom_size) len = (uint32_t)(rom_size - addr);
        uint8_t* tmpbuf = (uint8_t*)malloc(len);
        if (tmpbuf) {
            fseek(f, addr, SEEK_SET);
            if (fread(tmpbuf, 1, len, f) == len)
                memcpy(rom + addr, tmpbuf, len);   /* raw buffer bytes, no ^S8 */
            free(tmpbuf);
        }
        recs[w][0] = addr;
        recs[w][1] = len;
        ++w;
        i = j;
    }
    fclose(f);

    /* rewrite compacted idx */
    fi = fopen(idxpath, "wb");
    if (fi) {
        fwrite(recs, sizeof(*recs), w, fi);
        fclose(fi);
    }
    free(recs);
}

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define CART_ROM_ADDR_MASK UINT32_C(0x0fffffff);


void init_cart_rom(struct cart_rom* cart_rom,
                   uint8_t* rom, size_t rom_size,
                   struct r4300_core* r4300,
                   struct pi_controller* pi)
{
    cart_rom->rom = rom;
    cart_rom->rom_size = rom_size;

    cart_rom->r4300 = r4300;
    cart_rom->pi = pi;

    // Restore persisted writable-ROM ranges before emulation starts
    writable_cartrom_load_persisted(rom, rom_size);
}

void close_cart_rom(struct cart_rom* cart_rom)
{
    (void)cart_rom;
    if (l_writable_rom_file) {
        fclose(l_writable_rom_file);
        l_writable_rom_file = NULL;
    }
    if (l_writable_idx_file) {
        fclose(l_writable_idx_file);
        l_writable_idx_file = NULL;
    }
}

void poweron_cart_rom(struct cart_rom* cart_rom)
{
    cart_rom->last_write = 0;
}


void read_cart_rom(void* opaque, uint32_t address, uint32_t* value)
{
    struct cart_rom* cart_rom = (struct cart_rom*)opaque;
    uint32_t addr = rom_address(address);

    if (cart_rom->pi->regs[PI_STATUS_REG] & PI_STATUS_IO_BUSY)
    {
        *value = cart_rom->last_write;
    }
    else
    {
        *value = *(uint32_t*)(cart_rom->rom + addr);
    }
}

void write_cart_rom(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct cart_rom* cart_rom = (struct cart_rom*)opaque;
    cart_rom->last_write = value & mask;

    // Per-game writable ROM (ROM DB "WritableROM") - game writes to cart ROM space persist
    uint32_t addr = rom_address(address);
    extern m64p_rom_settings ROM_SETTINGS;
    if (ROM_SETTINGS.writablecartrom) {
        if (addr + 4 <= cart_rom->rom_size) {
            masked_write((uint32_t*)(cart_rom->rom + addr), value, mask);
            writable_cartrom_persist_range(addr, 4, cart_rom->rom, cart_rom->rom_size);
            if (!validate_pi_request(cart_rom->pi))
                return;
            cart_rom->pi->regs[PI_STATUS_REG] |= PI_STATUS_IO_BUSY;
            cp0_update_count(cart_rom->r4300);
            add_interrupt_event(&cart_rom->r4300->cp0, PI_INT, 0x1000);
            return;
        }
    }

    if (!validate_pi_request(cart_rom->pi))
        return;

    /* Mark IO as busy */
    cart_rom->pi->regs[PI_STATUS_REG] |= PI_STATUS_IO_BUSY;
    cp0_update_count(cart_rom->r4300);
    add_interrupt_event(&cart_rom->r4300->cp0, PI_INT, 0x1000);
}

unsigned int cart_rom_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    cart_addr &= CART_ROM_ADDR_MASK;

    // Per-game writable ROM (ROM DB "WritableROM"): the hack bakes the DD disk into ROM
    // and patches reads, but writes still target ROM cart space. Persist them.
    extern m64p_rom_settings ROM_SETTINGS;
    if (ROM_SETTINGS.writablecartrom) {
        struct cart_rom* cart_rom = (struct cart_rom*)opaque;
        size_t i;
        uint8_t* mem = cart_rom->rom;
        if (cart_addr + length <= cart_rom->rom_size) {
            for (i = 0; i < length; ++i) {
                mem[(cart_addr+i)^S8] = dram[(dram_addr+i)^S8];
            }
            writable_cartrom_persist_range(cart_addr, length, mem, cart_rom->rom_size);
            invalidate_r4300_cached_code(cart_rom->r4300, 0x80000000 + dram_addr, length);
            invalidate_r4300_cached_code(cart_rom->r4300, 0xa0000000 + dram_addr, length);
            DebugMessage(M64MSG_VERBOSE, "cart_rom DMA write persisted: dram 0x%08x cart 0x%08x len %u", dram_addr, cart_addr, length);
            return /* length / 8 */0x1000;
        }
    }

    DebugMessage(M64MSG_WARNING, "DMA Writing to CART_ROM: 0x%" PRIX32 " -> 0x%" PRIX32 " (0x%" PRIX32 ")", dram_addr, cart_addr, length);

    return /* length / 8 */0x1000;
}

unsigned int cart_rom_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    size_t i;
    struct cart_rom* cart_rom = (struct cart_rom*)opaque;
    const uint8_t* mem = cart_rom->rom;

    cart_addr &= CART_ROM_ADDR_MASK;

    if (cart_addr + length < cart_rom->rom_size)
    {
        for(i = 0; i < length; ++i) {
            dram[(dram_addr+i)^S8] = mem[(cart_addr+i)^S8];
        }
    }
    else
    {
        unsigned int diff = (cart_rom->rom_size <= cart_addr)
            ? 0
            : cart_rom->rom_size - cart_addr;

        for (i = 0; i < diff; ++i) {
            dram[(dram_addr+i)^S8] = mem[(cart_addr+i)^S8];
        }
        for (; i < length; ++i) {
            dram[(dram_addr+i)^S8] = 0;
        }
    }

    /* invalidate cached code */
    invalidate_r4300_cached_code(cart_rom->r4300, 0x80000000 + dram_addr, length);
    invalidate_r4300_cached_code(cart_rom->r4300, 0xa0000000 + dram_addr, length);

    return (length / 8) + add_random_interrupt_time(cart_rom->r4300);
}

