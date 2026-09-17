/*
 * Host fixture for the non-linking-terminator boundary rule.
 *
 * Include the production implementation so this covers the exact target scan
 * used by new_recompile_block(), not a copied model of it.  Section GC drops
 * the unrelated dynarec paths from this narrow host executable.
 */
#define NEW_DYNAREC 4
#define ARCH_MIN_SSE 0

#include "device/device.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

struct device g_dev;

/* The fixture never initializes the code cache. */
#define WIN32 1
typedef unsigned long DWORD;
typedef int BOOL;
#define MEM_RELEASE 0
static inline BOOL VirtualFree(void *address, size_t size, int type)
{
    (void)address;
    (void)size;
    (void)type;
    return 1;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "device/r4300/new_dynarec/new_dynarec.c"
#pragma GCC diagnostic pop

static void test_nonlink_terminator_boundary(void)
{
    const int decision_index = 4;

    start = UINT32_C(0x800bb540);
    memset(ba, 0xff, sizeof(ba));

    /*
     * With the policy clear, retain the legacy scan: a branch to the
     * terminating instruction (the delay-slot boundary) reopens the block.
     */
    ba[1] = start + decision_index * 4;
    assert(SetDdRuntimePolicy(0) == M64ERR_SUCCESS);
    assert(dd_dynarec_nonlink_block_continues(decision_index));

    /*
     * DD policy keeps the non-linking terminator as the boundary.  The helper
     * only decides continuation; delay-slot assembly remains production code.
     */
    assert(SetDdRuntimePolicy(1) == M64ERR_SUCCESS);
    assert(!dd_dynarec_nonlink_block_continues(decision_index));

    /* Clearing policy restores ordinary-title behavior. */
    assert(SetDdRuntimePolicy(0) == M64ERR_SUCCESS);
    assert(dd_dynarec_nonlink_block_continues(decision_index));

    memset(ba, 0xff, sizeof(ba));
    assert(!dd_dynarec_nonlink_block_continues(decision_index));
}

int main(void)
{
    test_nonlink_terminator_boundary();
    return 0;
}