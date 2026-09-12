#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "device/r4300/dd_fault_layout.h"

int main(void)
{
    /* Matches the core's uint32_t guest-word storage, with distinct halves. */
    uint32_t dram[0x128 / 4] = {0};
    const uint32_t base = UINT32_C(0x80000000);
    uint32_t word;
    uint64_t wide;
    unsigned int slot;

    assert(DD_FAULT_LAYOUT_CONTEXT_PREFIX == 0x128);
    assert(DD_FAULT_LAYOUT_GPR_OFFSET == 0x20);
    assert(DD_FAULT_LAYOUT_GPR_COUNT == 29);
    assert(DD_FAULT_LAYOUT_SP_OFFSET == 0xf0);
    assert(DD_FAULT_LAYOUT_RA_OFFSET == 0x100);
    assert(DD_FAULT_LAYOUT_LO_OFFSET == 0x108);
    assert(DD_FAULT_LAYOUT_HI_OFFSET == 0x110);
    assert(DD_FAULT_LAYOUT_SR_OFFSET == 0x118);
    assert(DD_FAULT_LAYOUT_PC_OFFSET == 0x11c);
    assert(DD_FAULT_LAYOUT_CAUSE_OFFSET == 0x120);
    assert(DD_FAULT_LAYOUT_BADVADDR_OFFSET == 0x124);

    for (slot = 0; slot < 29; ++slot) {
        dram[(0x20 + slot * 8) / 4] = UINT32_C(0xabcdef00) + slot;
        dram[(0x24 + slot * 8) / 4] = UINT32_C(0x12345600) + slot;
    }
    dram[0x108 / 4] = 0x11223344; dram[0x10c / 4] = 0x55667788;
    dram[0x110 / 4] = 0x99aabbcc; dram[0x114 / 4] = 0xddeeff00;
    dram[0x118 / 4] = 0x80000001;
    dram[0x11c / 4] = 0x800ad4ac;
    dram[0x120 / 4] = 0x80000010;
    dram[0x124 / 4] = 0x800d4203;

    for (slot = 0; slot < DD_FAULT_LAYOUT_GPR_COUNT; ++slot) {
        assert(dd_fault_guest_read_u64(dram, sizeof(dram),
            base + DD_FAULT_LAYOUT_GPR_OFFSET + slot * 8, &wide));
        assert(wide == (((UINT64_C(0xabcdef00) + slot) << 32)
            | (UINT64_C(0x12345600) + slot)));
    }
    /* Slots 26/28 are SP/RA; slot 0 is at, not v0. */
    assert(dd_fault_guest_read_u64(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_SP_OFFSET, &wide));
    assert(wide == UINT64_C(0xabcdef1a1234561a));
    assert(dd_fault_guest_read_u64(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_RA_OFFSET, &wide));
    assert(wide == UINT64_C(0xabcdef1c1234561c));
    assert(dd_fault_guest_read_u64(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_LO_OFFSET, &wide));
    assert(wide == UINT64_C(0x1122334455667788));
    assert(dd_fault_guest_read_u64(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_HI_OFFSET, &wide));
    assert(wide == UINT64_C(0x99aabbccddeeff00));
    assert(dd_fault_guest_read_u32(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_SR_OFFSET, &word) && word == 0x80000001);
    assert(dd_fault_guest_read_u32(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_PC_OFFSET, &word) && word == 0x800ad4ac);
    assert(dd_fault_guest_read_u32(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_CAUSE_OFFSET, &word) && word == 0x80000010);
    assert(dd_fault_guest_read_u32(dram, sizeof(dram),
        base + DD_FAULT_LAYOUT_BADVADDR_OFFSET, &word) && word == 0x800d4203);

    /* Exact end, truncated fields, KSEG1 alias, non-KSEG and MMIO rejection. */
    assert(dd_fault_guest_range(dram, sizeof(dram), base, 0x128, NULL));
    assert(!dd_fault_guest_range(dram, 0x127, base, 0x128, NULL));
    assert(dd_fault_guest_read_u32(dram, sizeof(dram), 0xa0000124, &word));
    assert(word == 0x800d4203);
    assert(dd_fault_guest_read_u64(dram, sizeof(dram), base + 0x120, &wide));
    assert(wide == UINT64_C(0x80000010800d4203));
    assert(!dd_fault_guest_read_u32(dram, 0x127, base + 0x124, &word));
    assert(!dd_fault_guest_read_u32(dram, sizeof(dram), base + 0x128, &word));
    assert(!dd_fault_guest_read_u64(dram, sizeof(dram), base + 0x124, &wide));
    assert(!dd_fault_guest_read_u32(dram, sizeof(dram), base + 1, &word));
    assert(!dd_fault_guest_read_u32(dram, sizeof(dram), 0x00000020, &word));
    assert(!dd_fault_guest_read_u32(dram, sizeof(dram), 0xc0000020, &word));
    assert(!dd_fault_guest_read_u32(dram, sizeof(dram), 0xa5000508, &word));
    assert(!dd_fault_guest_read_u64(dram, sizeof(dram), 0xfffffffc, &wide));
    assert(!dd_fault_guest_range(dram, sizeof(dram), base, SIZE_MAX, NULL));
    assert(!dd_fault_guest_read_u32(NULL, sizeof(dram), base, &word));
    assert(!dd_fault_guest_read_u64(dram, sizeof(dram), base, NULL));
    return 0;
}