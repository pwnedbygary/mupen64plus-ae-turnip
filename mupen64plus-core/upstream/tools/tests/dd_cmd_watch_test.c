/*
 * Host-only contract test for the bounded DD command-buffer writer probe.
 *
 * Example:
 *   cc -std=gnu99 -I../../src dd_cmd_watch_test.c \
 *      ../../src/device/dd/dd_cmd_watch.c -o dd_cmd_watch_test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/dd/dd_cmd_watch.h"

static int diagnostics_enabled;
static int runtime_policy;
static unsigned int callback_lines;
static char callback_last[1024];
static int callback_saw_sd;
static int callback_saw_pc;

static void test_callback(void *context, int level, const char *message);

int DdStartupDiagnosticsEnabled(void)
{
    return diagnostics_enabled;
}

int DdRuntimePolicyGet(void)
{
    return runtime_policy;
}

ptr_DdStartupDiagnosticsCallback DdStartupDiagnosticsGetCallback(void **context)
{
    if (context != NULL)
        *context = NULL;
    return diagnostics_enabled && runtime_policy ? test_callback : NULL;
}

static void test_callback(void *context, int level, const char *message)
{
    (void)context;
    (void)level;
    ++callback_lines;
    (void)snprintf(callback_last, sizeof(callback_last), "%s", message);
    if (strstr(message, "width=8") != NULL
            && strstr(message, "before=0x1122334455667788") != NULL
            && strstr(message, "after=0x8877665544332211") != NULL)
        callback_saw_sd = 1;
    if (strstr(message, "pc=0x80001ffc") != NULL
            && strstr(message, "delay_slot=1") != NULL)
        callback_saw_pc = 1;
}

static void task_words(uint32_t *words, uint32_t base, uint32_t size)
{
    memset(words, 0, 16 * sizeof(*words));
    words[0] = 2;
    words[12] = base;
    words[13] = size;
}

int main(void)
{
    uint32_t words[16];
    unsigned int i;

    diagnostics_enabled = 1;
    runtime_policy = 1;
    dd_cmd_watch_reset();

    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 10, 1, 104);
    assert(!dd_cmd_watch_route_should_slow(UINT32_C(0x00411910), 4));
    assert(dd_cmd_watch_route_should_slow(UINT32_C(0x80411910), 4));
    assert(dd_cmd_watch_route_consume());
    assert(!dd_cmd_watch_route_consume());
    assert(!dd_cmd_watch_in_range(UINT32_C(0x00411910), 4));
    assert(dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));

    /* Rolling storage keeps the last 32, and counts the eight overwritten. */
    for (i = 0; i < 40; ++i)
        dd_cmd_watch_record_aligned(UINT32_C(0x80411910) + i * 4, 4,
            i, i + 1, UINT32_C(0x80001000) + i * 4);

    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 11, 1, 0);
    assert(callback_lines >= 33);
    assert(strstr(callback_last, "coverage=0x0000003f") != NULL);

    /* Non-audio entries do not tear down the preceding range. */
    memset(words, 0, sizeof(words));
    dd_cmd_watch_task_entry(words, 12, 1, 104);
    assert(dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));

    /* The second range has an independent recent ring and SD metadata. */
    task_words(words, UINT32_C(0x004132d0), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 20, 1, 104);
    dd_cmd_watch_record_aligned(UINT32_C(0x804132d0), 8,
        UINT64_C(0x1122334455667788), UINT64_C(0x8877665544332211),
        UINT32_C(0x80002001));
    {
        unsigned int lines_before = callback_lines;
        task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
        dd_cmd_watch_task_entry(words, 21, 1, 0);
        assert(callback_lines == lines_before + 1);
        assert(strstr(callback_last, "recent=0") != NULL);
    }
    assert(!callback_saw_sd);
    task_words(words, UINT32_C(0x004132d0), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 22, 1, 0);
    assert(callback_saw_sd);
    assert(callback_saw_pc);

    /* A third identity reports replacement loss instead of dropping it. */
    dd_cmd_watch_record_aligned(UINT32_C(0x804132d0), 4, 1, 2,
        UINT32_C(0x80003000));
    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 30, 1, 104);
    task_words(words, UINT32_C(0x00415000), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 31, 1, 104);
    task_words(words, UINT32_C(0x00415000), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 32, 1, 0);
    assert(strstr(callback_last, "replaced_events=1 replacements=1") != NULL);

    runtime_policy = 0;
    assert(!dd_cmd_watch_route_should_slow(UINT32_C(0x00411910), 4));
    assert(!dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));
    runtime_policy = 1;
    assert(!dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));
    return 0;
}