BUILD_VARIANT := release

ifeq ($(NDK_DEBUG), 1)
    BUILD_VARIANT := debug
endif


# These static libraries keep the flags of the 2021 prebuilt archives that they replaced, and
# do not use the common flags below. ndk-build compiles each one only in the modules that
# link it.

#PNG: see ndkLibs/png/VENDOR.md
include $(CLEAR_VARS)
LOCAL_MODULE := png
PNG_DIR := $(JNI_LOCAL_PATH)/../ndkLibs/png
PNG_CONFIG_DIR := $(JNI_LOCAL_PATH)/../ndkLibs/config/png
LOCAL_SRC_FILES := $(addprefix $(PNG_DIR)/, png.c pngerror.c pngget.c pngmem.c pngpread.c \
    pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c \
    pngwtran.c pngwutil.c)
ifneq ($(filter armeabi-v7a arm64-v8a,$(TARGET_ARCH_ABI)),)
LOCAL_SRC_FILES += $(addprefix $(PNG_DIR)/arm/, arm_init.c filter_neon_intrinsics.c \
    palette_neon_intrinsics.c)
endif
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
LOCAL_CFLAGS := -mfpu=neon -DPNG_ARM_NEON_OPT=2 -DPNG_ARM_NEON_API_SUPPORTED
endif
LOCAL_CFLAGS += -O3
LOCAL_C_INCLUDES := $(PNG_DIR) $(PNG_CONFIG_DIR)
LOCAL_EXPORT_C_INCLUDES := $(PNG_DIR)/ $(PNG_CONFIG_DIR)
LOCAL_EXPORT_LDLIBS := -lz
include $(BUILD_STATIC_LIBRARY)

#GL: EGLLoader
include $(CLEAR_VARS)
LOCAL_MODULE := EGLLoader
LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES := $(JNI_LOCAL_PATH)/../ndkLibs/GL/GL/EGLLoader.cpp
LOCAL_C_INCLUDES := $(JNI_LOCAL_PATH)/../ndkLibs/GL
LOCAL_CFLAGS := -DEGL
LOCAL_CPPFLAGS := -fvisibility-inlines-hidden -O3 -ffast-math -Wno-unused-value -std=c++11
LOCAL_EXPORT_C_INCLUDES := $(JNI_LOCAL_PATH)/../ndkLibs/GL
include $(BUILD_STATIC_LIBRARY)

#SDL_net
include $(CLEAR_VARS)
LOCAL_MODULE := SDL2_net
LOCAL_SRC_FILES := $(JNI_LOCAL_PATH)/../ndkLibs/libs/$(BUILD_VARIANT)/$(TARGET_ARCH_ABI)/libSDL2_net.so
LOCAL_EXPORT_C_INCLUDES := $(JNI_LOCAL_PATH)/../ndkLibs/SDL_net/include/
include $(PREBUILT_SHARED_LIBRARY)

AE_BRIDGE_INCLUDES := $(JNI_LOCAL_PATH)/ae-bridge/
M64P_API_INCLUDES := $(JNI_LOCAL_PATH)/../mupen64plus-core/upstream/src/api/
GL_INCLUDES := $(JNI_LOCAL_PATH)/../ndkLibs/GL

COMMON_FLAGS := -Oz -fcommon -ffast-math -ftree-vectorize -fno-omit-frame-pointer

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
COMMON_FLAGS +=                     \
    -march=armv7-a                  \
    -mfloat-abi=softfp              \
    -mfpu=neon
endif

ifneq ($(BUILD_VARIANT), debug)
ifneq ($(HOST_OS),windows)
    COMMON_FLAGS += -flto
    COMMON_LDFLAGS += $(COMMON_FLAGS)
endif
endif

COMMON_CFLAGS := $(COMMON_FLAGS)    \
    -fvisibility=hidden -Wno-error=implicit-function-declaration

COMMON_CPPFLAGS := $(COMMON_FLAGS)    \
    -fvisibility-inlines-hidden

