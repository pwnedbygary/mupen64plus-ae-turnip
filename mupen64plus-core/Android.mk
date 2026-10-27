JNI_LOCAL_PATH := $(call my-dir)
include $(JNI_LOCAL_PATH)/../build_common/native_common.mk
# ndk-build makes the core's objects depend on this file, not on
# mupen64plus-core.mk, so after a change to the compiler flags there a build
# compiles them again only once this file changes or mupen64plus-core/build
# is deleted. Each flag change therefore also edits this comment; the latest
# turned fast math off.
include $(JNI_LOCAL_PATH)/mupen64plus-core.mk

