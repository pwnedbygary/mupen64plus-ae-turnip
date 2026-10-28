JNI_LOCAL_PATH := $(call my-dir)
include $(JNI_LOCAL_PATH)/../build_common/native_common.mk

# SDL_net (see ndkLibs/SDL_net/VENDOR.md) is declared here because only the core links it:
# a shared module in native_common.mk would be built by every module that includes that
# file. Like the static libraries there, it keeps the 2021 prebuilt's flags (ndk-build's
# defaults) instead of the common flags.
include $(CLEAR_VARS)
LOCAL_MODULE := SDL2_net
SDL_NET_DIR := $(JNI_LOCAL_PATH)/../ndkLibs/SDL_net
SDL_NET_CONFIG_DIR := $(JNI_LOCAL_PATH)/../ndkLibs/config/SDL_net
LOCAL_SRC_FILES := $(addprefix $(SDL_NET_DIR)/src/, SDLnet.c SDLnetTCP.c SDLnetUDP.c \
    SDLnetselect.c)
LOCAL_C_INCLUDES := $(SDL_NET_DIR)/include $(SDL_NET_DIR)/src $(SDL_NET_CONFIG_DIR)
LOCAL_CFLAGS := -DWITHOUT_SDL=1 -include $(SDL_NET_CONFIG_DIR)/SDL_net_libc.h
LOCAL_EXPORT_C_INCLUDES := $(SDL_NET_DIR)/include/
include $(BUILD_SHARED_LIBRARY)

# ndk-build makes the core's objects depend on this file, not on
# mupen64plus-core.mk, so after a change to the compiler flags there a build
# compiles them again only once this file changes or mupen64plus-core/build
# is deleted. Each flag change therefore also edits this comment; the latest
# turned fast math off.
include $(JNI_LOCAL_PATH)/mupen64plus-core.mk

