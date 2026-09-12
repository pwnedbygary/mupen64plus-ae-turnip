#ifndef DD_BOOT_POLICY_H
#define DD_BOOT_POLICY_H

#include <stddef.h>

/* The frontend enables this only for explicit DD support and a real cart
 * launch, never its dummy ROM used to launch a standalone disk. */
static inline int dd_combo_cart_boot(const char *option, size_t cart_size,
                                    size_t ipl_size)
{
    return option != NULL && option[0] == '1' && option[1] == '\0'
        && cart_size > 0 && ipl_size > 0;
}

#endif