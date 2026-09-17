/*
 * P09: pure source-region classifier for PI-to-RDMA DMA transfers.
 *
 * The helper under test tags every observed DDSTART16 record with the ROM window a a transfer came from, so analysis can tell a DD-ROM load apart from a plain cartridge-ROM load at capture time. This fixture drives that one pure function and asserts its contract against the N64DD memory-map rules, not against production constants:
 *
 *   - A source cart address inside the DOM2 window [MM_DOM2_ADDR1, MM_DOM2_ADDR2) is a DD-ROM transfer. The exact numeric edges live in device.h and are only used here to DERIVE test points (edges + midpoint), never as expected values.
 *   - Any other cartridge-PI source address is a plain cartridge-ROM transfer.
 *
 * Expected classifications come from the documented contract; the code under test supplies neither the windows nor the enum mapping, so this cannot pass by luck. The enum backing values (NONE=0, DD-ROM=1, CART_ROM=2) are asserted via an int cast of the returned enum rather than by spelling out its enumerators.
 */

#include <assert.h>

#include "device/dd/dd_load_history.h" /* helper decl + enum   */
#include "device/device.h"             /* DOM2 window bounds   */

int main(void)
{
    uint32_t lower = MM_DOM2_ADDR1;            /* first word of the window  */
    uint32_t upper = MM_DOM2_ADDR2;            /* one past last word        */

    /* Just below, exactly at, and deep inside the window => DD-ROM (== 1). */
    assert((int)dd_pi_dma_source_region(lower - 1) == 2);   /* just below edge      */
    assert((int)dd_pi_dma_source_region(lower)     == 1);   /* lower edge           */

    uint32_t mid = lower + (upper - lower) / 4;
    assert((int)dd_pi_dma_source_region(mid)       == 1);        /* deep inside          */

    uint32_t near_upper = upper - 1;
    assert((int)dd_pi_dma_source_region(near_upper)== 1);        /* just below upper edge*/

    /* Just above the window => CART-ROM (== 2): opposite side of the second edge. */
    assert((int)dd_pi_dma_source_region(upper)         == 2);/* upper edge           */
    assert((int)dd_pi_dma_source_region(upper + 0x1000)     == 2);/* above window         */

    return 0;
}
