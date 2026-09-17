/*
 * Exercise the core's production SP DMA register path.  In particular, this
 * keeps the byte-copy, row decode, source skip and bank-local SP address
 * wrap checks independent of diagnostics.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/rdram/rdram.h"
#include "plugin/plugin.h"

/*
 * rsp_core.c's normal task/status path is retained by write_rsp_regs.  The
 * DMA fixture does not execute a task, so provide only the surrounding core
 * services needed to link that production entry point.
 */
rsp_plugin_functions rsp;

static unsigned int event_type;
static unsigned int event_delay;
static unsigned int framebuffer_reads;
static unsigned int framebuffer_writes;

void DebugMessage(int level, const char *message, ...)
{
    va_list args;
    (void) level;
    (void) message;
    va_start(args, message);
    va_end(args);
}

void new_frame(void) {}
void protect_framebuffers(struct fb *fb) { (void) fb; }
void unprotect_framebuffers(struct fb *fb) { (void) fb; }

void pre_framebuffer_read(struct fb *fb, uint32_t address)
{
    (void) fb;
    (void) address;
    ++framebuffer_reads;
}

void post_framebuffer_write(struct fb *fb, uint32_t address, uint32_t length)
{
    (void) fb;
    (void) address;
    (void) length;
    ++framebuffer_writes;
}

void cp0_update_count(struct r4300_core *r4300) { (void) r4300; }

void add_interrupt_event(struct cp0 *cp0, int type, unsigned int delay)
{
    (void) cp0;
    event_type = (unsigned int) type;
    event_delay = delay;
}

unsigned int *get_event(const struct interrupt_queue *q, int type)
{
    (void) q;
    (void) type;
    return NULL;
}

void raise_rcp_interrupt(struct mi_controller *mi, uint32_t intr)
{
    mi->regs[MI_INTR_REG] |= intr;
}

void signal_rcp_interrupt(struct mi_controller *mi, uint32_t intr)
{
    mi->regs[MI_INTR_REG] |= intr;
}

void clear_rcp_interrupt(struct mi_controller *mi, uint32_t intr)
{
    mi->regs[MI_INTR_REG] &= ~intr;
}

static struct rsp_core sp;
static struct ri_controller ri;
static struct mi_controller mi;
static struct rdp_core dp;
static struct r4300_core r4300;
static struct rdram rdram;
static uint32_t spmem[SP_MEM_SIZE / sizeof(uint32_t)];
static uint32_t dram[0x800000 / sizeof(uint32_t)];

static int failures;

static unsigned char *sp_bytes(void)
{
    return (unsigned char *) spmem;
}

static unsigned char *dram_bytes(void)
{
    return (unsigned char *) dram;
}

static void fail(const char *case_name, unsigned int address,
                 unsigned int expected, unsigned int actual)
{
    fprintf(stderr, "%s: byte 0x%04x: expected 0x%02x, got 0x%02x\n",
            case_name, address, expected, actual);
    ++failures;
}

static void set_sp_byte(unsigned int bank, unsigned int offset,
                        unsigned char value)
{
    sp_bytes()[(bank | (offset & 0xfff)) ^ S8] = value;
}

static unsigned char get_sp_byte(unsigned int bank, unsigned int offset)
{
    return sp_bytes()[(bank | (offset & 0xfff)) ^ S8];
}

static void set_dram_byte(unsigned int address, unsigned char value)
{
    dram_bytes()[(address & 0xffffff) ^ S8] = value;
}

static unsigned char get_dram_byte(unsigned int address)
{
    return dram_bytes()[(address & 0xffffff) ^ S8];
}

static void reset_machine(void)
{
    memset(&sp, 0, sizeof(sp));
    memset(&ri, 0, sizeof(ri));
    memset(&mi, 0, sizeof(mi));
    memset(&dp, 0, sizeof(dp));
    memset(&r4300, 0, sizeof(r4300));
    memset(&rdram, 0, sizeof(rdram));
    memset(spmem, 0, sizeof(spmem));
    memset(dram, 0, sizeof(dram));

    ri.rdram = &rdram;
    rdram.dram = dram;
    rdram.dram_size = sizeof(dram);
    mi.r4300 = &r4300;
    r4300.rdram = &rdram;
    init_rsp(&sp, spmem, &mi, &dp, &ri);
    poweron_rsp(&sp);

    event_type = 0;
    event_delay = 0;
    framebuffer_reads = 0;
    framebuffer_writes = 0;
}

static void write_reg(enum sp_registers reg, uint32_t value)
{
    write_rsp_regs(&sp, (uint32_t)reg * 4, value, UINT32_MAX);
}

static void finish_dma(void)
{
    rsp_end_of_dma_event(&sp);
}

static void check_event(const char *case_name, unsigned int delay)
{
    if (event_type != RSP_DMA_EVT || event_delay != delay)
    {
        fprintf(stderr, "%s: expected RSP DMA event delay %u, got type 0x%x "
                "delay %u\n", case_name, delay, event_type, event_delay);
        ++failures;
    }
}

static void test_imem_wrap(void)
{
    unsigned int i;

    reset_machine();
    for (i = 0; i < 16; ++i)
        set_dram_byte(0x100 + i, (unsigned char)(0x40 + i));

    write_reg(SP_MEM_ADDR_REG, 0x1ff8);
    write_reg(SP_DRAM_ADDR_REG, 0x100);
    write_reg(SP_RD_LEN_REG, 0x0000000f);

    for (i = 0; i < 16; ++i)
    {
        unsigned char actual = get_sp_byte(0x1000, 0xff8 + i);
        if (actual != (unsigned char)(0x40 + i))
            fail("CPU-to-IMEM bank wrap", 0xff8 + i, 0x40 + i, actual);
    }
    check_event("CPU-to-IMEM bank wrap", 2);
    finish_dma();
}

static void test_count_and_aligned_skip(void)
{
    unsigned int row;
    unsigned int byte;

    reset_machine();
    for (byte = 0; byte < 8; ++byte)
    {
        set_dram_byte(0x200 + byte, (unsigned char)(0x60 + byte));
        set_dram_byte(0x218 + byte, (unsigned char)(0x70 + byte));
    }

    /*
     * Two eight-byte rows, with raw skip 0x13.  SP DMA skip is an eight-byte
     * quantity, so the second source row begins at 0x218, not 0x21b.
     */
    write_reg(SP_MEM_ADDR_REG, 0x0ff8);
    write_reg(SP_DRAM_ADDR_REG, 0x200);
    write_reg(SP_RD_LEN_REG, 0x01301007);

    for (row = 0; row < 2; ++row)
    {
        for (byte = 0; byte < 8; ++byte)
        {
            unsigned char expected = (unsigned char)(0x60 + row * 0x10 + byte);
            unsigned char actual = get_sp_byte(0, row * 8 + 0xff8 + byte);
            if (actual != expected)
                fail("CPU-to-DMEM count/skip", row * 8 + 0xff8 + byte,
                     expected, actual);
        }
    }
    check_event("CPU-to-DMEM count/skip", 2);
    finish_dma();
}

static void test_full_bank_transfer(void)
{
    unsigned int i;

    reset_machine();
    for (i = 0; i < 0x1000; ++i)
        set_dram_byte(0x1000 + i, (unsigned char)(i ^ 0xa5));

    write_reg(SP_MEM_ADDR_REG, 0x1800);
    write_reg(SP_DRAM_ADDR_REG, 0x1000);
    write_reg(SP_RD_LEN_REG, 0x00000fff);

    for (i = 0; i < 0x1000; ++i)
    {
        unsigned char expected = (unsigned char)(i ^ 0xa5);
        unsigned char actual = get_sp_byte(0x1000, 0x800 + i);
        if (actual != expected)
            fail("full IMEM transfer", 0x800 + i, expected, actual);
    }
    check_event("full IMEM transfer", 512);
    finish_dma();
}

static void test_sp_to_dram_wrap_and_skip(void)
{
    unsigned int row;
    unsigned int byte;

    reset_machine();
    for (row = 0; row < 2; ++row)
        for (byte = 0; byte < 8; ++byte)
            set_sp_byte(0x1000, 0xff8 + row * 8 + byte,
                        (unsigned char)(0x90 + row * 0x10 + byte));

    write_reg(SP_MEM_ADDR_REG, 0x1ff8);
    write_reg(SP_DRAM_ADDR_REG, 0x500);
    write_reg(SP_WR_LEN_REG, 0x02001007);

    for (row = 0; row < 2; ++row)
    {
        for (byte = 0; byte < 8; ++byte)
        {
            unsigned char expected = (unsigned char)(0x90 + row * 0x10 + byte);
            unsigned char actual = get_dram_byte(0x500 + row * 0x28 + byte);
            if (actual != expected)
                fail("SP-to-DRAM bank wrap/skip", 0x500 + row * 0x28 + byte,
                     expected, actual);
        }
    }
    if (framebuffer_writes != 2)
    {
        fprintf(stderr, "SP-to-DRAM bank wrap/skip: expected two framebuffer "
                "write notifications, got %u\n", framebuffer_writes);
        ++failures;
    }
    check_event("SP-to-DRAM bank wrap/skip", 2);
    finish_dma();
}

int main(void)
{
    test_imem_wrap();
    test_count_and_aligned_skip();
    test_full_bank_transfer();
    test_sp_to_dram_wrap_and_skip();

    if (failures != 0)
    {
        fprintf(stderr, "core RSP DMA fixture: %d failure(s)\n", failures);
        return 1;
    }

    puts("core RSP DMA fixture: all production-path checks passed");
    return 0;
}