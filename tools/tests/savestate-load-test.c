/* Host check that the core refuses save states it cannot load safely.
 *
 * A Project64 state gives the size of the RDRAM it holds. The loader trusted
 * it: a size of 0xFFFFD8AC or more wrapped the length it allocated for the
 * state to less than 0x2754 bytes, and it read past that buffer, at the ROM
 * header when the length was under 0x40 bytes, as for 0xFFFFD8AC, and
 * otherwise when that header matched the running game. A size of 18 MB or
 * more, in a file long enough to be read and made for the running game, set
 * up more RDRAM modules than the core keeps registers for. It now accepts
 * only 4 MB and 8 MB, the sizes the format supports.
 *
 * An M64P state holds the event queue as entries of a type and a count, and
 * the entry that starts with 0xFFFFFFFF ends it. The loader handed the queue
 * on without looking for that entry, and the queue is read until it, so a
 * queue without one was read past its end. It is now refused.
 *
 * The test includes savestates.c to reach its static save and load functions,
 * with stand-ins for the parts of the core that they call. It saves a
 * Project64 state with the core and loads it back, then loads states whose
 * start gives other sizes. It saves M64P states with the core, whose event
 * queues come from a stand-in, and loads them back, and it writes states of
 * version 1.0, whose queue is only as long as the file, by hand. A refused
 * M64P state must restore nothing: neither the first register the loader
 * restores nor the first word of RDRAM may change.
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

void StateChanged(m64p_core_param param_type, int new_value)
{
    (void)param_type;
    (void)new_value;
}

/* Stand-ins for the rest of the core. They keep the registers that the save
 * and load functions reach through accessors, and count the event queues
 * that a load hands on. */
CONTROL Controls[NUM_CONTROLLER];
gfx_plugin_functions gfx;
input_plugin_functions input;
m64p_rom_settings ROM_SETTINGS;

static uint32_t g_cp0_regs[CP0_REGS_COUNT], g_last_addr, g_pc, g_fcr0, g_fcr31;
static unsigned int g_next_interrupt, g_llbit, g_next_vi;
static int64_t g_regs[32], g_mult_hi, g_mult_lo;
static cp1_reg g_cp1_regs[32];
static int g_queue_loads;

uint32_t* r4300_cp0_regs(struct cp0* cp0) { (void)cp0; return g_cp0_regs; }
uint32_t* r4300_cp0_last_addr(struct cp0* cp0) { (void)cp0; return &g_last_addr; }
unsigned int* r4300_cp0_next_interrupt(struct cp0* cp0) { (void)cp0; return &g_next_interrupt; }
uint32_t* r4300_cp1_fcr0(struct cp1* cp1) { (void)cp1; return &g_fcr0; }
uint32_t* r4300_cp1_fcr31(struct cp1* cp1) { (void)cp1; return &g_fcr31; }
cp1_reg* r4300_cp1_regs(struct cp1* cp1) { (void)cp1; return g_cp1_regs; }
unsigned int* r4300_llbit(struct r4300_core* r4300) { (void)r4300; return &g_llbit; }
int64_t* r4300_mult_hi(struct r4300_core* r4300) { (void)r4300; return &g_mult_hi; }
int64_t* r4300_mult_lo(struct r4300_core* r4300) { (void)r4300; return &g_mult_lo; }
uint32_t* r4300_pc(struct r4300_core* r4300) { (void)r4300; return &g_pc; }
int64_t* r4300_regs(struct r4300_core* r4300) { (void)r4300; return g_regs; }
unsigned int* get_event(const struct interrupt_queue* q, int type) { (void)q; (void)type; return &g_next_vi; }
void load_eventqueue_infos(struct cp0* cp0, const char *buf) { (void)cp0; (void)buf; g_queue_loads++; }

/* The event queue that the next M64P save writes. */
static unsigned int g_next_queue[256];

int save_eventqueue_infos(const struct cp0* cp0, char *buf)
{
    (void)cp0;
    memcpy(buf, g_next_queue, sizeof(g_next_queue));
    return sizeof(g_next_queue);
}
void savestates_load_set_pc(struct r4300_core* r4300, uint32_t pc) { (void)r4300; (void)pc; }
void set_fpr_pointers(struct cp1* cp1, uint32_t newStatus) { (void)cp1; (void)newStatus; }
void update_x86_rounding_mode(struct cp1* cp1) { (void)cp1; }
void tlb_map(struct tlb* tlb, size_t entry) { (void)tlb; (void)entry; }
void poweron_dd(struct dd_controller* dd) { (void)dd; }
void poweron_fb(struct fb* fb) { (void)fb; }
void poweron_flashram(struct flashram* flashram) { (void)flashram; }
void poweron_gb_cart(struct gb_cart* gb_cart) { (void)gb_cart; }
void poweron_rumblepak(struct rumblepak* rpk) { (void)rpk; }
void poweron_transferpak(struct transferpak* tpk) { (void)tpk; }
void set_rumble_reg(struct rumblepak* rpk, uint8_t value) { (void)rpk; (void)value; }
void setup_channels_format(struct pif* pif) { (void)pif; }
size_t setup_pif_channel(struct pif_channel* channel, uint8_t* buf, size_t size) { (void)channel; (void)buf; (void)size; return 0; }
void disable_pif_channel(struct pif_channel* channel) { (void)channel; }
static void vi_changed(void) { }

static struct device g_test_dev;
static uint8_t g_rom_header[0x40] = "a ROM header that the state must match";

static struct device* test_device(void)
{
    struct device* dev = &g_test_dev;
    size_t i;

    dev->rdram.dram_size = 0x400000;
    dev->rdram.dram = calloc(1, dev->rdram.dram_size);
    dev->sp.mem = calloc(1, SP_MEM_SIZE);
    dev->pif.ram = calloc(1, PIF_RAM_SIZE);
    dev->cart.cart_rom.rom = g_rom_header;
    /* An M64P state names the ROM it was saved from. */
    memcpy(ROM_SETTINGS.MD5, "0123456789ABCDEF0123456789ABCDEF", 32);
    gfx.viStatusChanged = vi_changed;
    gfx.viWidthChanged = vi_changed;
    /* A load resets the controllers that the input plugin does not handle. */
    for (i = 0; i < NUM_CONTROLLER; ++i)
        Controls[i].RawData = 1;
    return dev;
}

static void report(const char *run, int failures_before)
{
    printf("%s: %s\n", run, g_failures == failures_before ? "ok" : "FAILED");
}

static int starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* The start of a Project64 state: its magic, 0x23D8A6C8 as a little-endian
 * word, then the size of its RDRAM, also little-endian. Nothing follows. */
static void write_pj64_start(const char *path, uint32_t rdram_size)
{
    const unsigned char start[8] = {
        0xc8, 0xa6, 0xd8, 0x23,
        rdram_size & 0xff, (rdram_size >> 8) & 0xff, (rdram_size >> 16) & 0xff, rdram_size >> 24
    };
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL && fwrite(start, 1, sizeof(start), f) == sizeof(start));
    if (f != NULL)
        fclose(f);
}

static char g_dir[512], g_path[600], g_m64p_path[600];

/* Also runs when a failed check stops the test early. */
static void remove_files(void)
{
    unlink(g_path);
    unlink(g_m64p_path);
    rmdir(g_dir);
}

static void check_pj64_start(struct device *dev, const char *run, uint32_t rdram_size, const char *message)
{
    int before = g_failures;

    write_pj64_start(g_path, rdram_size);
    g_message[0] = '\0';
    g_queue_loads = 0;
    CHECK(savestates_load_pj64_unc(dev, g_path) == 0);
    CHECK(starts_with(g_message, message));
    CHECK(g_queue_loads == 0);
    report(run, before);
}

/* Set before each M64P load in the first register that the loader restores
 * and in the first word of RDRAM. The states hold 0 in both, so a load that
 * restores the machine clears them. */
#define MACHINE_MARK 0x4d41524bu

/* Loads the M64P state at g_m64p_path. A refused one must give the message
 * and restore nothing. */
static void load_m64p(struct device *dev, int loads, const char *message)
{
    dev->rdram.regs[0][RDRAM_CONFIG_REG] = MACHINE_MARK;
    dev->rdram.dram[0] = MACHINE_MARK;
    g_message[0] = '\0';
    g_queue_loads = 0;
    CHECK(savestates_load_m64p(dev, g_m64p_path) == loads);
    CHECK(g_queue_loads == loads);
    CHECK(dev->rdram.regs[0][RDRAM_CONFIG_REG] == (loads ? 0 : MACHINE_MARK));
    CHECK(dev->rdram.dram[0] == (loads ? 0 : MACHINE_MARK));
    if (!loads)
        CHECK(starts_with(g_message, message));
}

/* Saves an M64P state whose queue has one VI interrupt, with the given count
 * and then the given second entry type, and loads it back. */
static void check_m64p(struct device *dev, const char *run, unsigned int count,
                       unsigned int second_type, int loads)
{
    int before = g_failures;

    memset(g_next_queue, 0, sizeof(g_next_queue));
    g_next_queue[0] = VI_INT;
    g_next_queue[1] = count;
    g_next_queue[2] = second_type;
    dev->rdram.regs[0][RDRAM_CONFIG_REG] = 0;
    dev->rdram.dram[0] = 0;
    CHECK(savestates_save_m64p(dev, g_m64p_path) == 1);
    load_m64p(dev, loads, "Invalid event queue in savestate");
    report(run, before);
}

/* Writes an M64P state of version 1.0 for the running ROM: a header, the
 * machine as 16788244 bytes of 0, then the first bytes of a queue with one VI
 * interrupt and an end entry. */
static void write_m64p_1_0(size_t queue_bytes)
{
    static const unsigned char version[4] = { 0x00, 0x01, 0x00, 0x00 };
    const unsigned int queue[3] = { VI_INT, 1000, 0xffffffff };
    const size_t machine_bytes = 16788244;
    unsigned char *machine = calloc(1, machine_bytes);
    gzFile f = gzopen(g_m64p_path, "wb");

    CHECK(machine != NULL && f != NULL && queue_bytes <= sizeof(queue));
    if (machine != NULL && f != NULL && queue_bytes <= sizeof(queue)) {
        CHECK(gzwrite(f, savestate_magic, 8) == 8);
        CHECK(gzwrite(f, version, 4) == 4);
        CHECK(gzwrite(f, ROM_SETTINGS.MD5, 32) == 32);
        CHECK(gzwrite(f, machine, machine_bytes) == (int)machine_bytes);
        if (queue_bytes > 0)
            CHECK(gzwrite(f, queue, queue_bytes) == (int)queue_bytes);
    }
    if (f != NULL)
        gzclose(f);
    free(machine);
}

static void check_m64p_1_0(struct device *dev, const char *run, size_t queue_bytes, int loads,
                           const char *message)
{
    int before = g_failures;

    write_m64p_1_0(queue_bytes);
    load_m64p(dev, loads, message);
    report(run, before);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    struct device *dev = test_device();
    int before;

    snprintf(g_dir, sizeof(g_dir), "%s/savestate-load-test-XXXXXX", tmp != NULL ? tmp : "/tmp");
    if (mkdtemp(g_dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(g_path, sizeof(g_path), "%s/state.pj", g_dir);
    snprintf(g_m64p_path, sizeof(g_m64p_path), "%s/state.st0", g_dir);
    atexit(remove_files);
    savestates_init();

    before = g_failures;
    CHECK(savestates_save_pj64_unc(dev, g_path) == 1);
    g_queue_loads = 0;
    CHECK(savestates_load_pj64_unc(dev, g_path) == 1);
    CHECK(g_queue_loads == 1);
    report("Project64 state saved by the core loads", before);

    check_pj64_start(dev, "4 MB Project64 state passes the size check", 0x400000,
                     "Could not read savestate data");
    check_pj64_start(dev, "8 MB Project64 state passes the size check", 0x800000,
                     "Could not read savestate data");
    check_pj64_start(dev, "6 MB Project64 state is refused", 0x600000,
                     "Invalid Project64 state RDRAM size.");
    check_pj64_start(dev, "16 MB Project64 state is refused", 0x1000000,
                     "Invalid Project64 state RDRAM size.");
    /* With the 0x2754 bytes that follow the RDRAM, this size makes the
     * state's length wrap around to 0 in 32 bits. */
    check_pj64_start(dev, "Project64 state whose length wraps is refused", 0xffffd8ac,
                     "Invalid Project64 state RDRAM size.");

    check_m64p(dev, "M64P state with the queue's end entry loads", 1000, 0xffffffff, 1);
    check_m64p(dev, "M64P state without it is refused", 1000, 0, 0);
    check_m64p(dev, "M64P state with 0xFFFFFFFF only as a count is refused", 0xffffffff, 0, 0);
    check_m64p_1_0(dev, "M64P 1.0 state whose queue ends at its last 4 bytes loads", 12, 1, "");
    check_m64p_1_0(dev, "M64P 1.0 state whose queue stops before its end entry is refused", 8, 0,
                   "Invalid event queue in savestate");
    check_m64p_1_0(dev, "M64P 1.0 state with no queue is refused", 0, 0,
                   "Invalid event queue in savestate");
    check_m64p_1_0(dev, "M64P 1.0 state whose queue stops inside an entry is refused", 6, 0,
                   "Could not read Mupen64Plus savestate 1.0 data");

    savestates_deinit();
    return g_failures != 0;
}
