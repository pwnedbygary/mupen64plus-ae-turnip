/*
 * Stand-in for <windows.h> in new-dynarec-boundary-test.c, which defines
 * WIN32 so that new_dynarec.c leaves out the ARM64 cache instructions an x86
 * host cannot assemble. Under WIN32, new_dynarec.c includes <windows.h> and
 * asks it for the page size; the test defines the other Windows names that
 * new_dynarec.c uses, and never sets up the dynarec code cache.
 */
#ifndef DYNAREC_TEST_WINDOWS_H
#define DYNAREC_TEST_WINDOWS_H

typedef struct { unsigned long dwPageSize; } SYSTEM_INFO;

static inline void GetSystemInfo(SYSTEM_INFO* info)
{
    info->dwPageSize = 4096;
}

#endif
