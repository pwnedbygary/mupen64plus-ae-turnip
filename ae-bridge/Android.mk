JNI_LOCAL_PATH := $(call my-dir)
include $(JNI_LOCAL_PATH)/../build_common/native_common.mk

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
include $(CLEAR_VARS)
LOCAL_MODULE := adrenotools
LOCAL_C_INCLUDES := \
    $(JNI_LOCAL_PATH)/../adrenotools/include \
    $(JNI_LOCAL_PATH)/../adrenotools/src \
    $(JNI_LOCAL_PATH)/../adrenotools/lib/linkernsbypass
LOCAL_SRC_FILES := \
    $(JNI_LOCAL_PATH)/../adrenotools/src/driver.cpp \
    $(JNI_LOCAL_PATH)/../adrenotools/src/bcenabler.cpp \
    $(JNI_LOCAL_PATH)/../adrenotools/lib/linkernsbypass/android_linker_ns.cpp \
    $(JNI_LOCAL_PATH)/../adrenotools/lib/linkernsbypass/elf_soname_patcher.cpp
LOCAL_CFLAGS := $(COMMON_CFLAGS)
LOCAL_CPPFLAGS := $(COMMON_CPPFLAGS) -std=c++20 -g
LOCAL_LDLIBS := -ldl -landroid
include $(BUILD_STATIC_LIBRARY)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := ae-bridge
LOCAL_STATIC_LIBRARIES := EGLLoader
LOCAL_C_INCLUDES := $(M64P_API_INCLUDES) $(GL_INCLUDES)
LOCAL_SRC_FILES := $(JNI_LOCAL_PATH)/src/ae_bridge.cpp
LOCAL_CFLAGS := $(COMMON_CFLAGS) -DEGL
LOCAL_CPPFLAGS := $(COMMON_CPPFLAGS) -std=c++11 -g
LOCAL_LDFLAGS := $(COMMON_LDFLAGS)
LOCAL_LDLIBS := -llog -lEGL -landroid

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_STATIC_LIBRARIES += adrenotools
    LOCAL_C_INCLUDES += \
        $(JNI_LOCAL_PATH)/../adrenotools/include \
        $(JNI_LOCAL_PATH)/../adrenotools/lib/linkernsbypass
    LOCAL_LDLIBS += -ldl
endif
include $(BUILD_SHARED_LIBRARY)