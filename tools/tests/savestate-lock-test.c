/* Host check that a failed save state write releases the save-state lock.
 *
 * The app's core build does not define M64P_PARALLEL, so queue_work runs the
 * write on the emulation thread that asked for the save (workqueue.h). That
 * thread's next M64P save, or its next load of an M64P state that exists,
 * takes the same lock, a default pthread mutex, so a failed write that kept
 * the lock left the thread waiting for itself.
 *
 * The test includes savestates.c to reach its static write function. The
 * first run writes a state, the next two make the write fail (a missing
 * folder, then a full device), and the last checks that saving still works.
 */
#include <stdarg.h>
#include <sys/time.h> /* the SDL stub calls gettimeofday without including it */
#include <unistd.h>

#include "main/savestates.c"

static int g_failures;
static char g_message[1024];

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            g_failures++;                                                   \
        }                                                                   \
    } while (0)

void main_message(m64p_msg_level level, unsigned int osd_corner, const char *format, ...)
{
    va_list args;
    (void)level;
    (void)osd_corner;
    va_start(args, format);
    vsnprintf(g_message, sizeof(g_message), format, args);
    va_end(args);
}

void DebugMessage(int level, const char *message, ...)
{
    (void)level;
    (void)message;
}

/* Bytes zlib cannot shrink, so a large enough block makes it write to the
 * file before gzwrite returns. */
static char *state_bytes(size_t size)
{
    char *data = malloc(size);
    uint32_t x = 2463534242u;
    size_t i;
    for (i = 0; i < size; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        data[i] = (char)(x >> 24);
    }
    return data;
}

/* Saves the way savestates_save_m64p hands a state to queue_work. */
static void save_state(const char *path, size_t size)
{
    struct savestate_work *save = malloc(sizeof(*save));
    save->filepath = strdup(path);
    save->data = state_bytes(size);
    save->size = size;
    g_message[0] = '\0';
    init_work(&save->work, savestates_save_m64p_work);
    queue_work(&save->work);
}

/* With the lock still held, the next save would wait for itself, so stop. */
static void check_lock_free(const char *run)
{
    if (SDL_TryLockMutex(savestates_lock) != 0) {
        fprintf(stderr, "FAIL %s: the save-state lock is still held\n", run);
        exit(1);
    }
    SDL_UnlockMutex(savestates_lock);
}

static void report(const char *run, int failures_before)
{
    printf("%s: %s\n", run, g_failures == failures_before ? "ok" : "FAILED");
}

static int starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int file_holds_state(const char *path, size_t size)
{
    char *expected = state_bytes(size);
    char *actual = malloc(size + 1);
    gzFile f = gzopen(path, "rb");
    int read = f != NULL ? gzread(f, actual, (unsigned)size + 1) : -1;
    int same = read >= 0 && (size_t)read == size && memcmp(actual, expected, size) == 0;
    if (f != NULL)
        gzclose(f);
    free(expected);
    free(actual);
    return same;
}

static char g_dir[512], g_written[600], g_after[600];

/* Also runs when a held lock stops the test early. */
static void remove_files(void)
{
    unlink(g_written);
    unlink(g_after);
    rmdir(g_dir);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    const char *written = g_written, *after = g_after;
    char missing[600];
    const size_t size = 1 << 20;
    int before;

    snprintf(g_dir, sizeof(g_dir), "%s/savestate-lock-test-XXXXXX", tmp != NULL ? tmp : "/tmp");
    if (mkdtemp(g_dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(g_written, sizeof(g_written), "%s/written.st0", g_dir);
    snprintf(missing, sizeof(missing), "%s/missing/state.st0", g_dir);
    snprintf(g_after, sizeof(g_after), "%s/after.st0", g_dir);
    atexit(remove_files);

    savestates_init();

    before = g_failures;
    save_state(written, size);
    check_lock_free("state written");
    CHECK(strcmp(g_message, "Saved state to: written.st0") == 0);
    CHECK(file_holds_state(written, size));
    report("state written", before);

    before = g_failures;
    save_state(missing, size);
    check_lock_free("folder missing");
    CHECK(starts_with(g_message, "Could not open state file"));
    report("folder missing", before);

    if (access("/dev/full", W_OK) == 0) {
        before = g_failures;
        save_state("/dev/full", size);
        check_lock_free("device full");
        CHECK(starts_with(g_message, "Could not write data to state file"));
        report("device full", before);
    } else {
        printf("device full: skipped, no /dev/full\n");
    }

    before = g_failures;
    save_state(after, size);
    check_lock_free("state written after the failures");
    CHECK(strcmp(g_message, "Saved state to: after.st0") == 0);
    CHECK(file_holds_state(after, size));
    report("state written after the failures", before);

    savestates_deinit();
    return g_failures != 0;
}
