#include "parallel_imp.h"
#include <chrono>
#include <memory>
#include <unistd.h>
#include <vector>
#include "rdp_device.hpp"
#include "context.hpp"
#include "device.hpp"
#include "plugin_filesystem.hpp"
#include "gfx_m64p.h"
#include "logging.hpp"

using namespace Vulkan;
using namespace std;

static int cmd_cur;
static int cmd_ptr;
static uint32_t cmd_data[0x00040000 >> 2];

/* ===========================================================================
   ROUND 39: NEVER DROP AN RDP WINDOW.

   vk_process_commands() below copies the window [DPC_CURRENT, DPC_END) into
   cmd_data (0x8000 64-bit commands) and walks it.  Upstream refuses any
   window that does not fit:

       if ((cmd_ptr + length) & ~(0x0003FFFF >> 3))
           return;                       // > 0x7FFF commands: silently dropped

   and that early return leaves DPC_CURRENT behind.  MEASURED CONSEQUENCE on
   the 64DD route (F-Zero X EK, rounds 36..38, wd_lowsp/wd_r25k0/wd_r36):

     * the EK's FIFO gfx ucode kicks the RDP with `mtc0 DMEM[0xF0], DPC_END`
       and then spins on DPC_CURRENT before publishing its next DMEM command
       block (IMEM 0x2A0..0x2B0: `mfc0 t3,DPC_CURRENT / sub t3,t3,t8 /
       blez / sub t3,t3,s3 / blez 0x2A0`);
     * a dropped window freezes that spin.  The pending region keeps growing,
       so the next flush's length keeps growing with it (measured: 672, 680,
       688, ... 1616 bytes, i.e. +8 per flush);
     * that DMA's destination is DMEM 0x9B0 and 0x9B0 + 0x650 = 0x1000, so
       once the length passes 0x650 the transfer overwrites DMEM 0xFC0..0xFFF
       -- THE OSTask COPY.  The ucode then reads its ring base/end/data_ptr
       out of display-list filler (0x00010001, and GBI words such as
       0xFC000640), the walk pointer k0 becomes 0xFF8000A7, DPC_START/END go
       to 0xFC000000-based values, and no FULLSYNC ever reaches the RDP again:
       measured DPC_START/END = 00000000/FC000640, FC000640/FC000C88,
       FC000C88/FC0012D8 as the last three kicks of the run.  The guest's gfx
       thread then waits forever for the DP message (fzerox-decomp
       src/sys/sys_main.c:352/396) -- the frozen 64DD screen.

   So: consume the window in chunks of at most 0x7FFF commands, publishing
   DPC_CURRENT after each chunk (hardware advances it as it consumes), and
   mask the RDRAM-side offset to the actual 8 MiB window -- upstream's
   `offset &= 0xFFFFF8` allows reads up to 16 MiB, i.e. off the end of the
   RDRAM allocation.

   R39_CHUNK=0 restores the upstream behavior for A/B (and latches every
   dropped window to files/wd_rdpdrop.txt so the mechanism stays measurable).
   A plain cart's windows are a few hundred commands, so this path is not
   reached at all; nothing here changes plain-game behavior.
   ======================================================================== */
#define R39_CHUNK 1
#define R39_DROP_FILE "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_rdpdrop.txt"

static void r39_drop_note(uint32_t cur, uint32_t end, unsigned length, unsigned cmds)
{
	static unsigned n = 0;
	FILE* f;
	if (n >= 8)
		return;
	n++;
	f = fopen(R39_DROP_FILE, (n == 1) ? "w" : "a");
	if (f == NULL)
		return;
	fprintf(f, "R39DROP n=%u cur=%08x end=%08x bytes=%u cmds=%u\n",
	        n, cur, end, length, cmds);
	fclose(f);
}

/* ROUND 57: OPT-IN.  The round 47/48 traces below were written
   unconditionally, so every plain game also produced up to 400 lines of
   wd_r47win.txt per launch -- the same plain-route diagnostic leak that round
   39 closed for wd_r31gen.txt.  The video plugin cannot see the core's
   g_dev.dd.idisk, so the gate is a flag file exactly like files/wd_trace.flag:
   the DD runs already touch wd_trace.flag, so they keep the trace, and a
   plain launch writes nothing.  Checked at most once a second (an access() on
   the RDP thread is far cheaper than the fopen this guards). */
#define R_VK_DIAG_FLAG "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_trace.flag"

static bool r_vk_diag_on()
{
	static int cached = -1;
	static unsigned tick = 0;
	if ((tick++ & 0x3ffu) == 0 || cached < 0)
		cached = (access(R_VK_DIAG_FLAG, F_OK) == 0) ? 1 : 0;
	return cached == 1;
}

/* ROUND 47: log EVERY ProcessRDPList call and the window it actually sees.
   The RSP-block-boundary samples in the RSP trace cannot answer this: the EK
   ucode kicks DPC_END and then spins on DPC_CURRENT, so a satisfied spin and a
   never-delivered kick look identical there.  This is the plugin's own view. */
#define R47_WIN_FILE "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r47win.txt"

static void r47_win_note(uint32_t cur, uint32_t end, int length)
{
	static unsigned n = 0;
	FILE* f;
	if (n >= 400 || !r_vk_diag_on())
		return;
	n++;
	f = fopen(R47_WIN_FILE, (n == 1) ? "w" : "a");
	if (f == NULL)
		return;
	fprintf(f, "R47WIN n=%u cur=%08x end=%08x bytes=%d cmds=%d%s\n",
	        n, cur, end, length, length > 0 ? length >> 3 : 0,
	        length <= 0 ? " EMPTY" : "");
	fclose(f);
}

/* ROUND 48: the incomplete-trailing-command branch. */
#define R48_INC_FILE "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r48inc.txt"

static void r48_inc_note(uint32_t cur, uint32_t end, int cmd_cur, int cmd_ptr, int cmd_length, uint32_t command)
{
	static unsigned n = 0;
	FILE* f;
	if (n >= 200 || !r_vk_diag_on())
		return;
	n++;
	f = fopen(R48_INC_FILE, (n == 1) ? "w" : "a");
	if (f == NULL)
		return;
	fprintf(f, "R48INC n=%u cur=%08x end=%08x cmd_cur=%d cmd_ptr=%d cmd_len=%d cmd=%02x "
	           "leftover_cmds=%d\n",
	        n, cur, end, cmd_cur, cmd_ptr, cmd_length, command, cmd_ptr - cmd_cur);
	fclose(f);
}

static unique_ptr<RDP::CommandProcessor> frontend;
static unique_ptr<Device> device;
static unique_ptr<Context> context;
static unique_ptr<DirFilesystem> plugin_cache_fs;

static PFN_vkGetInstanceProcAddr custom_vk_loader;
static std::chrono::steady_clock::time_point s_last_fence_timeout_log;

extern "C" __attribute__((visibility("default"))) void parallel_vulkan_loader_set(PFN_vkGetInstanceProcAddr addr)
{
	custom_vk_loader = addr;
}

int32_t vk_rescaling;
bool vk_ssreadbacks;
bool vk_ssdither;
bool running = false;
unsigned width, height;
unsigned vk_overscan;
unsigned vk_downscaling_steps;
bool vk_native_texture_lod;
bool vk_native_tex_rect;
bool vk_synchronous, vk_divot_filter, vk_gamma_dither;
bool vk_vi_aa, vk_vi_scale, vk_dither_filter;
bool vk_interlacing;

static const unsigned cmd_len_lut[64] = {
	1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
};

void vk_blit(unsigned &width, unsigned &height)
{
	if (running)
	{

		RDP::ScanoutOptions opts = {};
		opts.persist_frame_on_invalid_input = true;
		opts.vi.aa = vk_vi_aa;
		opts.vi.scale = vk_vi_scale;
		opts.vi.dither_filter = vk_dither_filter;
		opts.vi.divot_filter = vk_divot_filter;
		opts.vi.gamma_dither = vk_gamma_dither;
		opts.blend_previous_frame = vk_interlacing;
		opts.upscale_deinterlacing = !vk_interlacing;
		opts.downscale_steps = vk_downscaling_steps;
		opts.crop_overscan_pixels = vk_overscan;

		RDP::VIScanoutBuffer scanout;
		frontend->scanout_async_buffer(scanout, opts);

		if (!scanout.width || !scanout.height)
		{
			width = 0;
			height = 0;
			return;
		}

		width = scanout.width;
		height = scanout.height;

		if (!scanout.fence->wait_timeout(100'000'000ull))
		{
			auto now = std::chrono::steady_clock::now();
			if (now - s_last_fence_timeout_log >= std::chrono::milliseconds(5000))
			{
				s_last_fence_timeout_log = now;
				LOGW("scanout fence wait timed out (100ms), skipping frame\n");
			}
			width = 0;
			height = 0;
			return;
		}
		uint8_t* color_data = screen_get_texture_data();
		memcpy(color_data, device->map_host_buffer(*scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT),
			   width * height * sizeof(uint32_t));
		device->unmap_host_buffer(*scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
	}
}

void vk_rasterize()
{

	if (frontend && running)
	{
		frontend->set_vi_register(RDP::VIRegister::Control, *GET_GFX_INFO(VI_STATUS_REG));
		frontend->set_vi_register(RDP::VIRegister::Origin, *GET_GFX_INFO(VI_ORIGIN_REG));
		frontend->set_vi_register(RDP::VIRegister::Width, *GET_GFX_INFO(VI_WIDTH_REG));
		frontend->set_vi_register(RDP::VIRegister::Intr, *GET_GFX_INFO(VI_INTR_REG));
		frontend->set_vi_register(RDP::VIRegister::VCurrentLine, *GET_GFX_INFO(VI_V_CURRENT_LINE_REG));
		frontend->set_vi_register(RDP::VIRegister::Timing, *GET_GFX_INFO(VI_V_BURST_REG));
		frontend->set_vi_register(RDP::VIRegister::VSync, *GET_GFX_INFO(VI_V_SYNC_REG));
		frontend->set_vi_register(RDP::VIRegister::HSync, *GET_GFX_INFO(VI_H_SYNC_REG));
		frontend->set_vi_register(RDP::VIRegister::Leap, *GET_GFX_INFO(VI_LEAP_REG));
		frontend->set_vi_register(RDP::VIRegister::HStart, *GET_GFX_INFO(VI_H_START_REG));
		frontend->set_vi_register(RDP::VIRegister::VStart, *GET_GFX_INFO(VI_V_START_REG));
		frontend->set_vi_register(RDP::VIRegister::VBurst, *GET_GFX_INFO(VI_V_BURST_REG));
		frontend->set_vi_register(RDP::VIRegister::XScale, *GET_GFX_INFO(VI_X_SCALE_REG));
		frontend->set_vi_register(RDP::VIRegister::YScale, *GET_GFX_INFO(VI_Y_SCALE_REG));

		RDP::Quirks quirks;
		quirks.set_native_texture_lod(vk_native_texture_lod);
		quirks.set_native_resolution_tex_rect(vk_native_tex_rect);
		frontend->set_quirks(quirks);

		frontend->begin_frame_context();

		unsigned width = 0;
		unsigned height = 0;
		vk_blit(width, height);

		if (width == 0 || height == 0)
		{
			screen_swap(true);
			return;
		}

		struct frame_buffer buf = {0};
		buf.valid = true;
		buf.height = height;
		buf.width = width;
		buf.pitch = width;
		screen_write(&buf);
		screen_swap(false);
	}
}

void vk_process_commands()
{
	if (running)
	{

		const uint32_t DP_CURRENT = *GET_GFX_INFO(DPC_CURRENT_REG) & 0x00FFFFF8;
		const uint32_t DP_END = *GET_GFX_INFO(DPC_END_REG) & 0x00FFFFF8;

		int length = DP_END - DP_CURRENT;
		r47_win_note(DP_CURRENT, DP_END, length);
		if (length <= 0)
			return;

		length = unsigned(length) >> 3;
#if R39_CHUNK
		/* R39: chunked; see the note at the top of this file.  A single pass
		   over the upstream window is the common case (length <= 0x7FFF). */
		if ((cmd_ptr + length) & ~(0x0003FFFF >> 3))
			r39_drop_note(DP_CURRENT, DP_END, unsigned(length) << 3, length);
		{
			uint32_t offset = DP_CURRENT;
			uint32_t remaining = unsigned(length);
			const bool xbus = (*GET_GFX_INFO(DPC_STATUS_REG) & DP_STATUS_XBUS_DMA) != 0;

			while (remaining > 0)
			{
				uint32_t chunk = remaining > 0x7FFFu ? 0x7FFFu : remaining;
				uint32_t left = chunk;
				int incomplete = 0;

				cmd_ptr = 0;
				cmd_cur = 0;
				if (xbus)
				{
					do
					{
						offset &= 0xFF8;
						cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset);
						cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset + 4);
						offset += sizeof(uint64_t);
						cmd_ptr++;
					} while (--left > 0);
				}
				else
				{
					do
					{
						offset &= 0x7FFFF8;   /* R39: stay inside the 8 MiB RDRAM */
						cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(DRAM + offset);
						cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(DRAM + offset + 4);
						offset += sizeof(uint64_t);
						cmd_ptr++;
					} while (--left > 0);
				}

				while (cmd_cur - cmd_ptr < 0)
				{
					uint32_t w1 = cmd_data[2 * cmd_cur];
					uint32_t command = (w1 >> 24) & 63;
					int cmd_length = cmd_len_lut[command];

					if (cmd_ptr - cmd_cur - cmd_length < 0)
					{
						/* A command split across the chunk boundary.  Match
						   upstream's handling of a truncated tail: the window
						   is declared consumed. */
						incomplete = 1;
						break;
					}

					if (command >= 8 && frontend)
						frontend->enqueue_command(cmd_length * 2, &cmd_data[2 * cmd_cur]);

					if (RDP::Op(command) == RDP::Op::SyncFull)
					{
						// For synchronous RDP:
						if (vk_synchronous && frontend)
							frontend->wait_for_timeline(frontend->signal_timeline());
						*gfx.MI_INTR_REG |= DP_INTERRUPT;
						gfx.CheckInterrupts();
					}

					cmd_cur += cmd_length;
				}

				if (incomplete)
					break;

				remaining -= chunk;
				if (remaining > 0)
					*GET_GFX_INFO(DPC_CURRENT_REG) = offset & 0x00FFFFF8;
			}
		}
#else
		uint32_t offset = DP_CURRENT;
		if (*GET_GFX_INFO(DPC_STATUS_REG) & DP_STATUS_XBUS_DMA)
		{
			do
			{
				offset &= 0xFF8;
				cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset);
				cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset + 4);
				offset += sizeof(uint64_t);
				cmd_ptr++;
			} while (--length > 0);
		}
		else
		{
			if (DP_END > 0x7ffffff || DP_CURRENT > 0x7ffffff)
			{
				return;
			}
			else
			{
				do
				{
					offset &= 0xFFFFF8;
					cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(DRAM + offset);
					cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(DRAM + offset + 4);
					offset += sizeof(uint64_t);
					cmd_ptr++;
				} while (--length > 0);
			}
		}

		while (cmd_cur - cmd_ptr < 0)
		{
			uint32_t w1 = cmd_data[2 * cmd_cur];
			uint32_t command = (w1 >> 24) & 63;
			int cmd_length = cmd_len_lut[command];

			if (cmd_ptr - cmd_cur - cmd_length < 0)
			{
				/* ROUND 48: does this branch fire?  If it does, the trailing
				   command is DISCARDED while DPC_CURRENT is still advanced to
				   DPC_END -- so the ucode re-sends it next flush and the window
				   grows by one 8-byte command every time. */
				r48_inc_note(DP_CURRENT, DP_END, cmd_cur, cmd_ptr, cmd_length, command);
				*GET_GFX_INFO(DPC_START_REG) = *GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
				return;
			}

			if (command >= 8 && frontend)
				frontend->enqueue_command(cmd_length * 2, &cmd_data[2 * cmd_cur]);

			if (RDP::Op(command) == RDP::Op::SyncFull)
			{
				// For synchronous RDP:
				if (vk_synchronous && frontend)
					frontend->wait_for_timeline(frontend->signal_timeline());
				*gfx.MI_INTR_REG |= DP_INTERRUPT;
				gfx.CheckInterrupts();
			}

			cmd_cur += cmd_length;
		}
#endif

		cmd_ptr = 0;
		cmd_cur = 0;
		*GET_GFX_INFO(DPC_START_REG) = *GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
	}
}

void vk_destroy()
{
	running = false;
	frontend.reset();
	device.reset();
	plugin_cache_fs.reset();
	context.reset();

	screen_close();
}

bool vk_init()
{
	running = false;
	screen_init();
	context.reset(new Context);
	device.reset(new Device);
	frontend.reset();

	if (!::Vulkan::Context::init_loader(custom_vk_loader))
		return false;
	if (!context->init_instance_and_device(nullptr, 0, nullptr, 0, ::Vulkan::CONTEXT_CREATION_DISABLE_BINDLESS_BIT))
		return false;

	const char *cache_path = plugin_get_user_cache_path();
	if (cache_path != nullptr && cache_path[0] != '\0')
	{
		plugin_cache_fs = unique_ptr<DirFilesystem>(new DirFilesystem(cache_path));
		Context::SystemHandles handles;
		handles.filesystem = plugin_cache_fs.get();
		context->set_system_handles(handles);
	}

	uintptr_t aligned_rdram = reinterpret_cast<uintptr_t>(gfx.RDRAM);
	uintptr_t offset = 0;


	if (device->get_device_features().supports_external_memory_host)
	{
		size_t align = device->get_device_features().host_memory_properties.minImportedHostPointerAlignment;
		offset = aligned_rdram & (align - 1);

		if (offset)
		{
			return false;
		}
		aligned_rdram -= offset;
	}

	device->set_context(*context);
	device->init_frame_contexts(3);
	::RDP::CommandProcessorFlags flags = 0;

	switch (vk_rescaling)
	{
	case 1:
		break;
	case 2:
		flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT;
		break;
	case 4:
		flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT;
		break;
	case 8:
		flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT;
		break;

	default:
		break;
	}
	if (vk_rescaling >1 && vk_ssreadbacks)
		flags |= RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT;
	if (vk_ssdither)
		flags |= RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT;

	frontend.reset(new RDP::CommandProcessor(*device, reinterpret_cast<void *>(aligned_rdram),
											 offset, rdram_size, rdram_size / 2, flags));
	if (!frontend->device_is_supported())
	{
		frontend.reset();
		return false;
	}

	RDP::Quirks quirks;
	quirks.set_native_texture_lod(vk_native_texture_lod);
	quirks.set_native_resolution_tex_rect(vk_native_tex_rect);
	frontend->set_quirks(quirks);

	running = true;
	return true;
}
