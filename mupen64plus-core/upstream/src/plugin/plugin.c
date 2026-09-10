/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - plugin.c                                                *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2002 Hacktarux                                          *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/callbacks.h"
#include "api/m64p_common.h"
#include "api/m64p_plugin.h"
#include "api/m64p_types.h"
#include "device/memory/memory.h"
#include "device/r4300/cp0.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/ai/ai_controller.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/rcp/vi/vi_controller.h"
#include "dummy_audio.h"
#include "dummy_input.h"
#include "dummy_rsp.h"
#include "dummy_video.h"
#include "main/main.h"
#include "main/rom.h"
#include "main/version.h"
#include "osal/dynamiclib.h"
#include "plugin.h"

CONTROL Controls[4];

/* global function pointers - initialized on core startup */
gfx_plugin_functions gfx;
audio_plugin_functions audio;
input_plugin_functions input;
rsp_plugin_functions rsp;

/* local data structures and functions */
static const gfx_plugin_functions dummy_gfx = {
    dummyvideo_PluginGetVersion,
    dummyvideo_ChangeWindow,
    dummyvideo_InitiateGFX,
    dummyvideo_MoveScreen,
    dummyvideo_ProcessDList,
    dummyvideo_ProcessRDPList,
    dummyvideo_RomClosed,
    dummyvideo_RomOpen,
    dummyvideo_ShowCFB,
    dummyvideo_UpdateScreen,
    dummyvideo_ViStatusChanged,
    dummyvideo_ViWidthChanged,
    dummyvideo_ReadScreen2,
    dummyvideo_SetRenderingCallback,
    dummyvideo_ResizeVideoOutput,
    dummyvideo_FBRead,
    dummyvideo_FBWrite,
    dummyvideo_FBGetFrameBufferInfo
};

static const audio_plugin_functions dummy_audio = {
    dummyaudio_PluginGetVersion,
    dummyaudio_AiDacrateChanged,
    dummyaudio_AiLenChanged,
    dummyaudio_InitiateAudio,
    dummyaudio_ProcessAList,
    dummyaudio_RomClosed,
    dummyaudio_RomOpen,
    dummyaudio_SetSpeedFactor,
    dummyaudio_VolumeUp,
    dummyaudio_VolumeDown,
    dummyaudio_VolumeGetLevel,
    dummyaudio_VolumeSetLevel,
    dummyaudio_VolumeMute,
    dummyaudio_VolumeGetString
};

static const input_plugin_functions dummy_input = {
    dummyinput_PluginGetVersion,
    dummyinput_ControllerCommand,
    dummyinput_GetKeys,
    dummyinput_InitiateControllers,
    dummyinput_ReadController,
    dummyinput_RomClosed,
    dummyinput_RomOpen,
    dummyinput_SDL_KeyDown,
    dummyinput_SDL_KeyUp,
    dummyinput_RenderCallback
};

static const rsp_plugin_functions dummy_rsp = {
    dummyrsp_PluginGetVersion,
    dummyrsp_DoRspCycles,
    dummyrsp_InitiateRSP,
    dummyrsp_RomClosed
};

static GFX_INFO gfx_info;
static AUDIO_INFO audio_info;
static CONTROL_INFO control_info;
static RSP_INFO rsp_info;

/* ROUND 10: the DMEM/IMEM/RDRAM pointers most recently published to the RSP
   plugin.  See plugin_refresh_rsp_memory_if_moved() below. */
static unsigned char* l_rsp_mem_published = NULL;

static int l_RspAttached = 0;
static int l_InputAttached = 0;
static int l_AudioAttached = 0;
static int l_GfxAttached = 0;

static unsigned int dummy;

/* local functions */
static void EmptyFunc(void)
{
}

// Handy macro to avoid code bloat when loading symbols
#define GET_FUNC(type, field, name) \
    ((field = (type)osal_dynlib_getproc(plugin_handle, name)) != NULL)

// code to handle backwards-compatibility to video plugins with API_VERSION < 02.1.0.  This API version introduced a boolean
// flag in the rendering callback, which told the core whether or not the current screen has been freshly redrawn since the
// last time the callback was called.
static void                     (*l_mainRenderCallback)(int) = NULL;
static ptr_SetRenderingCallback   l_old1SetRenderingCallback = NULL;

static void backcompat_videoRenderCallback(int unused)  // this function will be called by the video plugin as the render callback
{
    if (l_mainRenderCallback != NULL)
        l_mainRenderCallback(1);  // assume screen is always freshly redrawn (otherwise screenshots won't work w/ OSD enabled)
}

static void backcompat_setRenderCallbackIntercept(void (*callback)(int))
{
    l_mainRenderCallback = callback;
}

static void plugin_disconnect_gfx(void)
{
    gfx = dummy_gfx;
    l_GfxAttached = 0;
    l_mainRenderCallback = NULL;
}

static m64p_error plugin_connect_gfx(m64p_dynlib_handle plugin_handle)
{
    /* attach the Video plugin function pointers */
    if (plugin_handle != NULL)
    {
        m64p_plugin_type PluginType;
        int PluginVersion, APIVersion;

        if (l_GfxAttached)
            return M64ERR_INVALID_STATE;

        /* set function pointers for required functions */
        if (!GET_FUNC(ptr_PluginGetVersion, gfx.getVersion, "PluginGetVersion") ||
            !GET_FUNC(ptr_ChangeWindow, gfx.changeWindow, "ChangeWindow") ||
            !GET_FUNC(ptr_InitiateGFX, gfx.initiateGFX, "InitiateGFX") ||
            !GET_FUNC(ptr_MoveScreen, gfx.moveScreen, "MoveScreen") ||
            !GET_FUNC(ptr_ProcessDList, gfx.processDList, "ProcessDList") ||
            !GET_FUNC(ptr_ProcessRDPList, gfx.processRDPList, "ProcessRDPList") ||
            !GET_FUNC(ptr_RomClosed, gfx.romClosed, "RomClosed") ||
            !GET_FUNC(ptr_RomOpen, gfx.romOpen, "RomOpen") ||
            !GET_FUNC(ptr_ShowCFB, gfx.showCFB, "ShowCFB") ||
            !GET_FUNC(ptr_UpdateScreen, gfx.updateScreen, "UpdateScreen") ||
            !GET_FUNC(ptr_ViStatusChanged, gfx.viStatusChanged, "ViStatusChanged") ||
            !GET_FUNC(ptr_ViWidthChanged, gfx.viWidthChanged, "ViWidthChanged") ||
            !GET_FUNC(ptr_ReadScreen2, gfx.readScreen, "ReadScreen2") ||
            !GET_FUNC(ptr_SetRenderingCallback, gfx.setRenderingCallback, "SetRenderingCallback") ||
            !GET_FUNC(ptr_FBRead, gfx.fBRead, "FBRead") ||
            !GET_FUNC(ptr_FBWrite, gfx.fBWrite, "FBWrite") ||
            !GET_FUNC(ptr_FBGetFrameBufferInfo, gfx.fBGetFrameBufferInfo, "FBGetFrameBufferInfo"))
        {
            DebugMessage(M64MSG_ERROR, "broken Video plugin; function(s) not found.");
            plugin_disconnect_gfx();
            return M64ERR_INPUT_INVALID;
        }

        /* set function pointers for optional functions */
        gfx.resizeVideoOutput = (ptr_ResizeVideoOutput)osal_dynlib_getproc(plugin_handle, "ResizeVideoOutput");

        /* check the version info */
        (*gfx.getVersion)(&PluginType, &PluginVersion, &APIVersion, NULL, NULL);
        if (PluginType != M64PLUGIN_GFX || (APIVersion & 0xffff0000) != (GFX_API_VERSION & 0xffff0000))
        {
            DebugMessage(M64MSG_ERROR, "incompatible Video plugin");
            plugin_disconnect_gfx();
            return M64ERR_INCOMPATIBLE;
        }

        /* handle backwards-compatibility */
        if (APIVersion < 0x020100)
        {
            DebugMessage(M64MSG_WARNING, "Fallback for Video plugin API (%02i.%02i.%02i) < 2.1.0. Screenshots may contain On Screen Display text", VERSION_PRINTF_SPLIT(APIVersion));
            // tell the video plugin to make its rendering callback to me (it's old, and doesn't have the bScreenRedrawn flag)
            gfx.setRenderingCallback(backcompat_videoRenderCallback);
            l_old1SetRenderingCallback = gfx.setRenderingCallback; // save this just for future use
            gfx.setRenderingCallback = (ptr_SetRenderingCallback) backcompat_setRenderCallbackIntercept;
        }
        if (APIVersion < 0x20200 || gfx.resizeVideoOutput == NULL)
        {
            DebugMessage(M64MSG_WARNING, "Fallback for Video plugin API (%02i.%02i.%02i) < 2.2.0. Resizable video will not work", VERSION_PRINTF_SPLIT(APIVersion));
            gfx.resizeVideoOutput = dummyvideo_ResizeVideoOutput;
        }

        l_GfxAttached = 1;
    }
    else
        plugin_disconnect_gfx();

    return M64ERR_SUCCESS;
}

static m64p_error plugin_start_gfx(void)
{
    uint8_t media = *((uint8_t*)mem_base_u32(g_mem_base, MM_CART_ROM) + (0x3b ^ S8));

    /* Here we feed 64DD IPL ROM header to GFX plugin if 64DD is present.
     * We use g_media_loader.get_dd_rom to detect 64DD presence
     * instead of g_dev because the latter is not yet initialized at plugin_start time */
    /* XXX: Not sure it is the best way to convey which game is being played to the GFX plugin
     * as 64DD IPL is the same for all 64DD games... */
    char* dd_ipl_rom_filename = (g_media_loader.get_dd_rom == NULL)
        ? NULL
        : g_media_loader.get_dd_rom(g_media_loader.cb_data);

    uint32_t rom_base = (dd_ipl_rom_filename != NULL && strlen(dd_ipl_rom_filename) != 0 && media != 'C')
        ? MM_DD_ROM
        : MM_CART_ROM;

    free(dd_ipl_rom_filename);

    /* fill in the GFX_INFO data structure */
    gfx_info.HEADER = (unsigned char *)mem_base_u32(g_mem_base, rom_base);
    gfx_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    gfx_info.DMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    gfx_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);
    gfx_info.MI_INTR_REG = &(g_dev.mi.regs[MI_INTR_REG]);
    gfx_info.DPC_START_REG = &(g_dev.dp.dpc_regs[DPC_START_REG]);
    gfx_info.DPC_END_REG = &(g_dev.dp.dpc_regs[DPC_END_REG]);
    gfx_info.DPC_CURRENT_REG = &(g_dev.dp.dpc_regs[DPC_CURRENT_REG]);
    gfx_info.DPC_STATUS_REG = &(g_dev.dp.dpc_regs[DPC_STATUS_REG]);
    gfx_info.DPC_CLOCK_REG = &(g_dev.dp.dpc_regs[DPC_CLOCK_REG]);
    gfx_info.DPC_BUFBUSY_REG = &(g_dev.dp.dpc_regs[DPC_BUFBUSY_REG]);
    gfx_info.DPC_PIPEBUSY_REG = &(g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG]);
    gfx_info.DPC_TMEM_REG = &(g_dev.dp.dpc_regs[DPC_TMEM_REG]);
    gfx_info.VI_STATUS_REG = &(g_dev.vi.regs[VI_STATUS_REG]);
    gfx_info.VI_ORIGIN_REG = &(g_dev.vi.regs[VI_ORIGIN_REG]);
    gfx_info.VI_WIDTH_REG = &(g_dev.vi.regs[VI_WIDTH_REG]);
    gfx_info.VI_INTR_REG = &(g_dev.vi.regs[VI_V_INTR_REG]);
    gfx_info.VI_V_CURRENT_LINE_REG = &(g_dev.vi.regs[VI_CURRENT_REG]);
    gfx_info.VI_TIMING_REG = &(g_dev.vi.regs[VI_BURST_REG]);
    gfx_info.VI_V_SYNC_REG = &(g_dev.vi.regs[VI_V_SYNC_REG]);
    gfx_info.VI_H_SYNC_REG = &(g_dev.vi.regs[VI_H_SYNC_REG]);
    gfx_info.VI_LEAP_REG = &(g_dev.vi.regs[VI_LEAP_REG]);
    gfx_info.VI_H_START_REG = &(g_dev.vi.regs[VI_H_START_REG]);
    gfx_info.VI_V_START_REG = &(g_dev.vi.regs[VI_V_START_REG]);
    gfx_info.VI_V_BURST_REG = &(g_dev.vi.regs[VI_V_BURST_REG]);
    gfx_info.VI_X_SCALE_REG = &(g_dev.vi.regs[VI_X_SCALE_REG]);
    gfx_info.VI_Y_SCALE_REG = &(g_dev.vi.regs[VI_Y_SCALE_REG]);
    gfx_info.CheckInterrupts = EmptyFunc;

    gfx_info.version = 2; //Version 2 added SP_STATUS_REG and RDRAM_SIZE
    gfx_info.SP_STATUS_REG = &g_dev.sp.regs[SP_STATUS_REG];
    gfx_info.RDRAM_SIZE = (unsigned int*) &g_dev.rdram.dram_size;

    /* call the audio plugin */
    if (!gfx.initiateGFX(gfx_info))
        return M64ERR_PLUGIN_FAIL;

    return M64ERR_SUCCESS;
}

static void plugin_disconnect_audio(void)
{
    audio = dummy_audio;
    l_AudioAttached = 0;
}

static m64p_error plugin_connect_audio(m64p_dynlib_handle plugin_handle)
{
    /* attach the Audio plugin function pointers */
    if (plugin_handle != NULL)
    {
        m64p_plugin_type PluginType;
        int PluginVersion, APIVersion;

        if (l_AudioAttached)
            return M64ERR_INVALID_STATE;

        if (!GET_FUNC(ptr_PluginGetVersion, audio.getVersion, "PluginGetVersion") ||
            !GET_FUNC(ptr_AiDacrateChanged, audio.aiDacrateChanged, "AiDacrateChanged") ||
            !GET_FUNC(ptr_AiLenChanged, audio.aiLenChanged, "AiLenChanged") ||
            !GET_FUNC(ptr_InitiateAudio, audio.initiateAudio, "InitiateAudio") ||
            !GET_FUNC(ptr_ProcessAList, audio.processAList, "ProcessAList") ||
            !GET_FUNC(ptr_RomOpen, audio.romOpen, "RomOpen") ||
            !GET_FUNC(ptr_RomClosed, audio.romClosed, "RomClosed") ||
            !GET_FUNC(ptr_SetSpeedFactor, audio.setSpeedFactor, "SetSpeedFactor") ||
            !GET_FUNC(ptr_VolumeUp, audio.volumeUp, "VolumeUp") ||
            !GET_FUNC(ptr_VolumeDown, audio.volumeDown, "VolumeDown") ||
            !GET_FUNC(ptr_VolumeGetLevel, audio.volumeGetLevel, "VolumeGetLevel") ||
            !GET_FUNC(ptr_VolumeSetLevel, audio.volumeSetLevel, "VolumeSetLevel") ||
            !GET_FUNC(ptr_VolumeMute, audio.volumeMute, "VolumeMute") ||
            !GET_FUNC(ptr_VolumeGetString, audio.volumeGetString, "VolumeGetString"))
        {
            DebugMessage(M64MSG_ERROR, "broken Audio plugin; function(s) not found.");
            plugin_disconnect_audio();
            return M64ERR_INPUT_INVALID;
        }

        /* check the version info */
        (*audio.getVersion)(&PluginType, &PluginVersion, &APIVersion, NULL, NULL);
        if (PluginType != M64PLUGIN_AUDIO || (APIVersion & 0xffff0000) != (AUDIO_API_VERSION & 0xffff0000))
        {
            DebugMessage(M64MSG_ERROR, "incompatible Audio plugin");
            plugin_disconnect_audio();
            return M64ERR_INCOMPATIBLE;
        }

        l_AudioAttached = 1;
    }
    else
        plugin_disconnect_audio();

    return M64ERR_SUCCESS;
}

static m64p_error plugin_start_audio(void)
{
    /* fill in the AUDIO_INFO data structure */
    audio_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    audio_info.DMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    audio_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);
    audio_info.MI_INTR_REG = &(g_dev.mi.regs[MI_INTR_REG]);
    audio_info.AI_DRAM_ADDR_REG = &(g_dev.ai.regs[AI_DRAM_ADDR_REG]);
    audio_info.AI_LEN_REG = &(g_dev.ai.regs[AI_LEN_REG]);
    audio_info.AI_CONTROL_REG = &(g_dev.ai.regs[AI_CONTROL_REG]);
    audio_info.AI_STATUS_REG = &dummy;
    audio_info.AI_DACRATE_REG = &(g_dev.ai.regs[AI_DACRATE_REG]);
    audio_info.AI_BITRATE_REG = &(g_dev.ai.regs[AI_BITRATE_REG]);
    audio_info.CheckInterrupts = EmptyFunc;

    /* call the audio plugin */
    if (!audio.initiateAudio(audio_info))
        return M64ERR_PLUGIN_FAIL;

    return M64ERR_SUCCESS;
}

static void plugin_disconnect_input(void)
{
    input = dummy_input;
    l_InputAttached = 0;
}

static m64p_error plugin_connect_input(m64p_dynlib_handle plugin_handle)
{
    /* attach the Input plugin function pointers */
    if (plugin_handle != NULL)
    {
        m64p_plugin_type PluginType;
        int PluginVersion, APIVersion;

        if (l_InputAttached)
            return M64ERR_INVALID_STATE;

        if (!GET_FUNC(ptr_PluginGetVersion, input.getVersion, "PluginGetVersion") ||
            !GET_FUNC(ptr_ControllerCommand, input.controllerCommand, "ControllerCommand") ||
            !GET_FUNC(ptr_GetKeys, input.getKeys, "GetKeys") ||
            !GET_FUNC(ptr_InitiateControllers, input.initiateControllers, "InitiateControllers") ||
            !GET_FUNC(ptr_ReadController, input.readController, "ReadController") ||
            !GET_FUNC(ptr_RomOpen, input.romOpen, "RomOpen") ||
            !GET_FUNC(ptr_RomClosed, input.romClosed, "RomClosed") ||
            !GET_FUNC(ptr_SDL_KeyDown, input.keyDown, "SDL_KeyDown") ||
            !GET_FUNC(ptr_SDL_KeyUp, input.keyUp, "SDL_KeyUp"))
        {
            DebugMessage(M64MSG_ERROR, "broken Input plugin; function(s) not found.");
            plugin_disconnect_input();
            return M64ERR_INPUT_INVALID;
        }

        if (!GET_FUNC(ptr_SendVRUWord, input.sendVRUWord, "SendVRUWord") ||
            !GET_FUNC(ptr_SetMicState, input.setMicState, "SetMicState") ||
            !GET_FUNC(ptr_ReadVRUResults, input.readVRUResults, "ReadVRUResults") ||
            !GET_FUNC(ptr_ClearVRUWords, input.clearVRUWords, "ClearVRUWords") ||
            !GET_FUNC(ptr_SetVRUWordMask, input.setVRUWordMask, "SetVRUWordMask"))
        {
            DebugMessage(M64MSG_WARNING, "Input plugin does not contain VRU support.");
        }

        /* check the version info */
        (*input.getVersion)(&PluginType, &PluginVersion, &APIVersion, NULL, NULL);
        if (PluginType != M64PLUGIN_INPUT || (APIVersion & 0xffff0000) != (INPUT_API_VERSION & 0xffff0000) || APIVersion < 0x020100)
        {
            DebugMessage(M64MSG_ERROR, "incompatible Input plugin");
            plugin_disconnect_input();
            return M64ERR_INCOMPATIBLE;
        }

        if (!GET_FUNC(ptr_RenderCallback, input.renderCallback, "RenderCallback"))
        {
            DebugMessage(M64MSG_INFO, "input plugin did not specify a render callback; there will be no on screen display by the input plugin.");
        }

        l_InputAttached = 1;
    }
    else
        plugin_disconnect_input();

    return M64ERR_SUCCESS;
}

static m64p_error plugin_start_input(void)
{
    int i;

    /* fill in the CONTROL_INFO data structure */
    control_info.Controls = Controls;
    for (i=0; i<4; i++)
      {
         Controls[i].Present = 0;
         Controls[i].RawData = 0;
         Controls[i].Plugin = PLUGIN_NONE;
         Controls[i].Type = CONT_TYPE_STANDARD;
      }

    /* call the input plugin */
    input.initiateControllers(control_info);

    return M64ERR_SUCCESS;
}

static void plugin_disconnect_rsp(void)
{
    rsp = dummy_rsp;
    l_RspAttached = 0;
}

static m64p_error plugin_connect_rsp(m64p_dynlib_handle plugin_handle)
{
    /* attach the RSP plugin function pointers */
    if (plugin_handle != NULL)
    {
        m64p_plugin_type PluginType;
        int PluginVersion, APIVersion;

        if (l_RspAttached)
            return M64ERR_INVALID_STATE;

        if (!GET_FUNC(ptr_PluginGetVersion, rsp.getVersion, "PluginGetVersion") ||
            !GET_FUNC(ptr_DoRspCycles, rsp.doRspCycles, "DoRspCycles") ||
            !GET_FUNC(ptr_InitiateRSP, rsp.initiateRSP, "InitiateRSP") ||
            !GET_FUNC(ptr_RomClosed, rsp.romClosed, "RomClosed"))
        {
            DebugMessage(M64MSG_ERROR, "broken RSP plugin; function(s) not found.");
            plugin_disconnect_rsp();
            return M64ERR_INPUT_INVALID;
        }

        /* check the version info */
        (*rsp.getVersion)(&PluginType, &PluginVersion, &APIVersion, NULL, NULL);
        if (PluginType != M64PLUGIN_RSP || (APIVersion & 0xffff0000) != (RSP_API_VERSION & 0xffff0000))
        {
            DebugMessage(M64MSG_ERROR, "incompatible RSP plugin");
            plugin_disconnect_rsp();
            return M64ERR_INCOMPATIBLE;
        }

        l_RspAttached = 1;
    }
    else
        plugin_disconnect_rsp();

    return M64ERR_SUCCESS;
}

/* ForceSynchronize (ares forceSynchronize equivalent): the RSP microcode is
   spinning on a poll that needs a peripheral DMA/interrupt event to fire.  In
   the synchronous emulation model the CPU PC does not advance while the RSP
   task runs (DoRspCycles), so cp0_update_count() advances 0 and the queued
   event never becomes due.  Advance CP0 time to the next queued real
   peripheral event (SP/PI/AI/SI/DP/DD) so it dispatches naturally once the
   RSP yields.  VI/COMPARE re-queue themselves every dispatch, so skip pure
   VI/COMPARE heads to avoid accelerating game-time during a post-load spin.
   Plain cart games MUST be unaffected: no-op unless a 64DD disk is attached. */
static void rsp_force_synchronize(void)
{
    if (g_dev.dd.idisk == NULL)
        return; /* plain cart game: keep the stock behavior exactly */

    struct r4300_core* r4300 = &g_dev.r4300;
    struct cp0* cp0 = &r4300->cp0;
    uint32_t* cp0_regs = r4300_cp0_regs(cp0);

    struct node* n = cp0->q.first;
    while (n != NULL && (n->data.type == VI_INT || n->data.type == COMPARE_INT))
        n = n->next;

    if (n == NULL)
        return; /* only timer events pending: nothing to synchronize */

    uint32_t target = n->data.count;
    uint32_t cur = cp0_regs[CP0_COUNT_REG];
    if (target > cur)
    {
        cp0_regs[CP0_COUNT_REG] = target;
        *r4300_cp0_cycle_count(cp0) += (int)(target - cur);
    }
}

/* Runtime DD query for the RSP plugin: plugin_start_rsp runs BEFORE
   init_device, so a static wiring decision at that point would always see
   dd.idisk == NULL.  Evaluate on every call instead. */
static int rsp_is_dd_present(void)
{
    return (g_dev.dd.idisk != NULL) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
   ROUND-13 DD DIAG: count the RDP kicks that come from the RSP side.

   Every RDP command list produced by the gfx ucode is handed to the video
   plugin exactly here: the ares RSP executes the ucode's `mtc0 DPC_END`, its
   cp0.cpp calls RSP::rsp.ProcessRdpList, and that pointer is this core's
   rsp_info.ProcessRdpList.  parallel-RDP then raises the DP interrupt by
   writing *gfx.MI_INTR_REG |= 0x20 -- a DIRECT write to g_dev.mi's register,
   so it is invisible to the raise/signal counters in mi_controller.c (which
   only see core-originated interrupts).  Without this wrapper there was no
   way to tell "the ucode never reached DPC_END" from "the RDP ran but the
   guest never saw the interrupt" -- the two have completely different fixes.

   Counting is DD-gated; the wrapped call is exactly what the pointer was
   before, so plain games are behaviourally unchanged (one extra tail call). */
static void rsp_process_rdp_list(void)
{
    if (g_dev.dd.idisk != NULL)
    {
        extern volatile uint32_t wd_c_rdp_kick;
        extern uint32_t wd_rdp_last_start, wd_rdp_last_end, wd_rdp_last_mi, wd_rdp_last_sp;
        extern volatile uint32_t wd_c_rdp_dp_seen, wd_c_rdp_dp_hot, wd_c_rdp_empty;
        extern volatile uint32_t wd_c_rdp_noadv, wd_c_rdp_bad;
        extern volatile uint32_t wd_rdp_ring_n;
        extern uint32_t wd_rdp_ring[16][6];
        uint32_t mi_before = g_dev.mi.regs[MI_INTR_REG];
        /* ROUND 19: the LAST 16 kicks are printed by the watchdog, but the
           interesting question is whether the RDP was EVER handed a real list:
           by 50 s all 16 ring slots read start=cur=0xfffffff8 with end climbing
           in the low page, i.e. every kick is discarded before a command is
           examined and MI_INTR_DP can never be raised.  Keep the FIRST 16 too. */
        extern volatile uint32_t wd_rdp_first_n;
        extern uint32_t wd_rdp_first[16][6];
        uint32_t cur = g_dev.dp.dpc_regs[DPC_CURRENT_REG];
        uint32_t end = g_dev.dp.dpc_regs[DPC_END_REG];
        wd_c_rdp_kick++;
        wd_rdp_last_start = g_dev.dp.dpc_regs[DPC_START_REG];
        wd_rdp_last_end = end;
        wd_rdp_last_mi = mi_before;
        wd_rdp_last_sp = g_dev.sp.regs[SP_STATUS_REG];
        if ((int32_t)((end & 0x00FFFFF8u) - (cur & 0x00FFFFF8u)) <= 0)
            wd_c_rdp_empty++;
        if (mi_before & MI_INTR_DP)
            wd_c_rdp_dp_hot++;
        /* ROUND 20/21: DID THE RDP ACTUALLY TAKE THE WINDOW IT WAS HANDED?
           parallel-RDP (vk_process_commands) always finishes a call by setting
           DPC_START = DPC_CURRENT = DPC_END -- except on three silent early
           returns (length <= 0, the 0x8000-command capacity guard, and
           DP_END/DP_CURRENT above 0x7ffffff), which leave CURRENT behind.  The
           FIFO ucode's flush loop is built on that pointer advancing: after
           `mtc0 rdpFifoPos, DPC_END` it spins on DPC_CURRENT to know when the
           ring space is free again, and only then publishes the next 344-byte
           DMEM command block (`sw $11, rdpFifoPos` / dma_write).  A silently
           dropped window therefore hangs the ucode with the ring byte-forever
           empty -- exactly the observed state (ring all zeros, MI_INTR_DP
           never raised).  DD-gated; the wrapped call is untouched. */
        {
            uint32_t end_m = end & 0x00FFFFF8u;
            if ((g_dev.dp.dpc_regs[DPC_CURRENT_REG] & 0x00FFFFF8u) != end_m)
                wd_c_rdp_noadv++;
            if (end_m == 0 || (end & 0x00800000u) || (cur & 0x00800000u))
            {
                wd_c_rdp_bad++;
                if (wd_c_rdp_bad <= 4)
                {
                    static FILE* bf = NULL;
                    if (!bf) bf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_rdpbad.txt", "a");
                    if (bf)
                    {
                        fprintf(bf, "RDPBAD n=%u cur=%08x end=%08x start=%08x sp=%08x mi=%08x\n",
                            wd_c_rdp_bad, cur, end, g_dev.dp.dpc_regs[DPC_START_REG],
                            g_dev.sp.regs[SP_STATUS_REG], mi_before);
                        fflush(bf);
                    }
                }
            }
        }
        gfx.processRDPList();
        {
            uint32_t mi_after = g_dev.mi.regs[MI_INTR_REG];
            uint32_t* e = wd_rdp_ring[wd_rdp_ring_n & 15];
            if (mi_after & MI_INTR_DP)
                wd_c_rdp_dp_seen++;
            e[0] = wd_rdp_last_start;
            e[1] = cur;
            e[2] = end;
            e[3] = g_dev.dp.dpc_regs[DPC_STATUS_REG];
            e[4] = mi_before;
            e[5] = mi_after;
            wd_rdp_ring_n++;
            if (wd_rdp_first_n < 16)
            {
                uint32_t* q = wd_rdp_first[wd_rdp_first_n];
                q[0] = wd_rdp_last_start; q[1] = cur; q[2] = end;
                q[3] = e[3]; q[4] = mi_before; q[5] = mi_after;
                wd_rdp_first_n++;
            }
        }
        return;
    }
    gfx.processRDPList();
}

static m64p_error plugin_start_rsp(void)
{
    /* fill in the RSP_INFO data structure */    rsp_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    rsp_info.DMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    rsp_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);
    rsp_info.MI_INTR_REG = &g_dev.mi.regs[MI_INTR_REG];
    rsp_info.SP_MEM_ADDR_REG = &g_dev.sp.regs[SP_MEM_ADDR_REG];
    rsp_info.SP_DRAM_ADDR_REG = &g_dev.sp.regs[SP_DRAM_ADDR_REG];
    rsp_info.SP_RD_LEN_REG = &g_dev.sp.regs[SP_RD_LEN_REG];
    rsp_info.SP_WR_LEN_REG = &g_dev.sp.regs[SP_WR_LEN_REG];
    rsp_info.SP_STATUS_REG = &g_dev.sp.regs[SP_STATUS_REG];
    rsp_info.SP_DMA_FULL_REG = &g_dev.sp.regs[SP_DMA_FULL_REG];
    rsp_info.SP_DMA_BUSY_REG = &g_dev.sp.regs[SP_DMA_BUSY_REG];
    rsp_info.SP_PC_REG = &g_dev.sp.regs2[SP_PC_REG];
    rsp_info.SP_SEMAPHORE_REG = &g_dev.sp.regs[SP_SEMAPHORE_REG];
    rsp_info.DPC_START_REG = &g_dev.dp.dpc_regs[DPC_START_REG];
    rsp_info.DPC_END_REG = &g_dev.dp.dpc_regs[DPC_END_REG];
    rsp_info.DPC_CURRENT_REG = &g_dev.dp.dpc_regs[DPC_CURRENT_REG];
    rsp_info.DPC_STATUS_REG = &g_dev.dp.dpc_regs[DPC_STATUS_REG];
    rsp_info.DPC_CLOCK_REG = &g_dev.dp.dpc_regs[DPC_CLOCK_REG];
    rsp_info.DPC_BUFBUSY_REG = &g_dev.dp.dpc_regs[DPC_BUFBUSY_REG];
    rsp_info.DPC_PIPEBUSY_REG = &g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG];
    rsp_info.DPC_TMEM_REG = &g_dev.dp.dpc_regs[DPC_TMEM_REG];
    rsp_info.CheckInterrupts = EmptyFunc;
    rsp_info.ProcessDlistList = gfx.processDList;
    rsp_info.ProcessAlistList = audio.processAList;
    rsp_info.ProcessRdpList = rsp_process_rdp_list;
    rsp_info.ShowCFB = gfx.showCFB;
    /* ares forceSynchronize + runtime DD query, wired for every game:
       the parallel-RSP plugin keys its ares-derived work (yield protocol,
       JIT budget, clean completion) off IsDDPresent(), which is evaluated at
       TASK time — after init_device has set dd.idisk — so plain cart games
       get the stock path and 64DD games get the ares path.  rsp_force_synchronize
       itself no-ops without a disk (belt-and-suspenders). */
    rsp_info.ForceSynchronize = rsp_force_synchronize;
    rsp_info.IsDDPresent = rsp_is_dd_present;

    /* call the RSP plugin  */
    rsp.initiateRSP(rsp_info, NULL);
    l_rsp_mem_published = rsp_info.DMEM;

    return M64ERR_SUCCESS;
}

/* ROUND 10 -- re-publish the RSP memory base to the RSP plugin when it MOVED.

   mupen64plus hands an RSP plugin raw host pointers (RDRAM/DMEM/IMEM) once, in
   RSP_INFO at plugin_start_rsp time, and the plugin keeps them for its whole
   process lifetime.  The register pointers stay valid forever because they
   point into g_dev (a static global), but the MEMORY base comes from
   init_mem_base() in CoreStartup and is therefore per emulation session.  When
   a front-end starts a SECOND session in the same process -- which is exactly
   what the 64DD "combo boot" does, booting the IPL-ROM session and then the
   game session -- every memory pointer the plugin still holds refers to the
   previous session's buffer, and it fails SILENTLY: SP_STATUS still reads live
   values (g_dev again), while DMEM 0xFC0 reads 0 and IMEM reads all-zero.

   Measured on the RP6 (F-Zero X EK + disk, parallel-RSP, emumode=2): the core
   DMA'd the F3DEX ucode and saw IMEM[0] = 0x340a0fc0, while the plugin read
   RSP::rsp.IMEM[0] == 0 for the remaining 2744 RSP entries of the run -- so the
   RSP executed an empty IMEM, no GFX task ever completed, the RDP was never
   kicked, and the game's DP event never fired (raise_bits DP=0 for the whole
   run), leaving the EK's gfx thread parked in osRecvMesg(&D800DCAC8) and the DD
   loader frozen on its progress bar.

   Self-gating: it acts only when the base actually moved, so a normal single
   session (every plain cart) takes the early return and is bit-for-bit
   unaffected.  Called right after init_device(), before any RSP task runs, so
   re-running the plugin's initiateRSP (which resets SP_PC/SP_STATUS and
   re-binds the ares CPU's memory) cannot disturb live RSP state. */
int plugin_refresh_rsp_memory_if_moved(void)
{
    unsigned char* dmem;

    if (!l_RspAttached)
        return 0;

    dmem = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    if (dmem == l_rsp_mem_published)
        return 0;

    rsp_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    rsp_info.DMEM = dmem;
    rsp_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);

    {
        FILE* f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_init.txt", "a");
        if (f)
        {
            fprintf(f, "WDINIT refresh old_dmem=%p old_imem=%p new_dmem=%p new_imem=%p\n",
                    (void*)l_rsp_mem_published, (void*)(l_rsp_mem_published ? l_rsp_mem_published + 0x1000 : NULL),
                    (void*)rsp_info.DMEM, (void*)rsp_info.IMEM);
            fclose(f);
        }
    }

    l_rsp_mem_published = rsp_info.DMEM;
    rsp.initiateRSP(rsp_info, NULL);
    return 1;
}

/* global functions */
m64p_error plugin_connect(m64p_plugin_type type, m64p_dynlib_handle plugin_handle)
{
    switch(type)
    {
        case M64PLUGIN_GFX:
            if (plugin_handle != NULL && (l_AudioAttached || l_InputAttached || l_RspAttached))
                DebugMessage(M64MSG_WARNING, "Front-end bug: plugins are attached in wrong order.");
            return plugin_connect_gfx(plugin_handle);
        case M64PLUGIN_AUDIO:
            if (plugin_handle != NULL && (l_InputAttached || l_RspAttached))
                DebugMessage(M64MSG_WARNING, "Front-end bug: plugins are attached in wrong order.");
            return plugin_connect_audio(plugin_handle);
        case M64PLUGIN_INPUT:
            if (plugin_handle != NULL && (l_RspAttached))
                DebugMessage(M64MSG_WARNING, "Front-end bug: plugins are attached in wrong order.");
            return plugin_connect_input(plugin_handle);
        case M64PLUGIN_RSP:
            return plugin_connect_rsp(plugin_handle);
        default:
            return M64ERR_INPUT_INVALID;
    }

    return M64ERR_INTERNAL;
}

m64p_error plugin_start(m64p_plugin_type type)
{
    switch(type)
    {
        case M64PLUGIN_RSP:
            return plugin_start_rsp();
        case M64PLUGIN_GFX:
            return plugin_start_gfx();
        case M64PLUGIN_AUDIO:
            return plugin_start_audio();
        case M64PLUGIN_INPUT:
            return plugin_start_input();
        default:
            return M64ERR_INPUT_INVALID;
    }

    return M64ERR_INTERNAL;
}

m64p_error plugin_check(void)
{
    if (!l_GfxAttached)
        DebugMessage(M64MSG_WARNING, "No video plugin attached.  There will be no video output.");
    if (!l_RspAttached)
        DebugMessage(M64MSG_WARNING, "No RSP plugin attached.  The video output will be corrupted.");
    if (!l_AudioAttached)
        DebugMessage(M64MSG_WARNING, "No audio plugin attached.  There will be no sound output.");
    if (!l_InputAttached)
        DebugMessage(M64MSG_WARNING, "No input plugin attached.  You won't be able to control the game.");

    return M64ERR_SUCCESS;
}

