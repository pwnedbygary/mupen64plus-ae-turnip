#ifndef M64P_DD_FAULT_LAYOUT_H
#define M64P_DD_FAULT_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Guest ABI for the selected OSThread's saved fault context.  These values
 * are byte offsets in the N64 guest object; they must not be replaced by
 * host-ABI sizeof/offsetof expressions.
 *
 * The fault-context prefix ends immediately after BadVAddr at 0x128.  The
 * 29 saved integer registers begin at at, followed by v0/v1. Both the guest
 * header and libultra exception-save ABI include the at slot.
 */
enum
{
    DD_FAULT_LAYOUT_CONTEXT_PREFIX = 0x128,
    DD_FAULT_LAYOUT_GPR_OFFSET = 0x20,
    DD_FAULT_LAYOUT_GPR_COUNT = 29,
    DD_FAULT_LAYOUT_SP_OFFSET = 0xf0,
    DD_FAULT_LAYOUT_RA_OFFSET = 0x100,
    DD_FAULT_LAYOUT_LO_OFFSET = 0x108,
    DD_FAULT_LAYOUT_HI_OFFSET = 0x110,
    DD_FAULT_LAYOUT_SR_OFFSET = 0x118,
    DD_FAULT_LAYOUT_PC_OFFSET = 0x11c,
    DD_FAULT_LAYOUT_CAUSE_OFFSET = 0x120,
    DD_FAULT_LAYOUT_BADVADDR_OFFSET = 0x124
};

/*
 * Production and tests share these direct guest-word readers. RDRAM is the
 * core's uint32_t guest-word view, not a host-byte view. No memory handler,
 * MMIO access, or guest write is permitted here.
 */
static inline int dd_fault_guest_range(const uint32_t* dram, size_t size,
        uint32_t address, size_t bytes, uint32_t* physical)
{
    uint32_t segment = address & UINT32_C(0xe0000000);
    uint32_t offset = address & UINT32_C(0x1fffffff);
    if (dram == NULL || bytes == 0 || (address & UINT32_C(3)) != 0
            || (segment != UINT32_C(0x80000000)
                && segment != UINT32_C(0xa0000000))
            || (size_t)offset > size || size - (size_t)offset < bytes
            || bytes > (size_t)UINT32_C(0x20000000) - offset)
        return 0;
    if (physical != NULL)
        *physical = offset;
    return 1;
}

static inline int dd_fault_guest_read_u32(const uint32_t* dram, size_t size,
        uint32_t address, uint32_t* value)
{
    uint32_t physical;
    if (value == NULL || !dd_fault_guest_range(dram, size, address, 4,
                &physical))
        return 0;
    *value = dram[physical / 4];
    return 1;
}

static inline int dd_fault_guest_read_u64(const uint32_t* dram, size_t size,
        uint32_t address, uint64_t* value)
{
    uint32_t physical;
    if (value == NULL || !dd_fault_guest_range(dram, size, address, 8,
                &physical))
        return 0;
    *value = ((uint64_t)dram[physical / 4] << 32) | dram[physical / 4 + 1];
    return 1;
}

#endif