cmake_minimum_required(VERSION 3.22)

project( Audio-Android )

add_definitions(-DANDROID)

set( SOUNDTOUCH_INCLUDE_DIRS "${ANDROID_LIB_PATH}/soundtouch/include" )

# SoundTouch, from ndkLibs/soundtouch (its VENDOR.md explains the flags). In STTypes.h,
# __SOFTFP__ with ANDROID selects 16-bit integer samples; libsoundtouch_fp.so has float samples.
# The target options come after the module's -Oz and this file's -O3 -ffast-math, and override them.
set( SOUNDTOUCH_SOURCES AAFilter FIFOSampleBuffer FIRFilter cpu_detect_x86 sse_optimized
        RateTransposer SoundTouch InterpolateCubic InterpolateLinear InterpolateShannon TDStretch
        BPMDetect PeakFinder )
if( ANDROID_ABI MATCHES "^x86" )
    list( APPEND SOUNDTOUCH_SOURCES mmx_optimized )
endif()
list( TRANSFORM SOUNDTOUCH_SOURCES PREPEND "${ANDROID_LIB_PATH}/soundtouch/source/SoundTouch/" )
list( TRANSFORM SOUNDTOUCH_SOURCES APPEND ".cpp" )
foreach( lib IN ITEMS soundtouch soundtouch_fp )
    add_library( ${lib} SHARED ${SOUNDTOUCH_SOURCES} )
    target_include_directories( ${lib} PRIVATE ${SOUNDTOUCH_INCLUDE_DIRS} )
    target_compile_options( ${lib} PRIVATE -O2 -fno-fast-math -fexceptions -fno-rtti
            $<$<STREQUAL:${ANDROID_ABI},armeabi-v7a>:-marm> )
    target_link_libraries( ${lib} log )
endforeach()
target_compile_definitions( soundtouch PRIVATE __SOFTFP__ ANDROID )
target_compile_definitions( soundtouch_fp PRIVATE SOUNDTOUCH_FLOAT_SAMPLES ANDROID )


include_directories(
        ${SOUNDTOUCH_INCLUDE_DIRS}
        ${M64API_INCLUDE_PATH})

# Find the Oboe package
find_package (oboe REQUIRED CONFIG)

SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -O3 -ffast-math")

add_library (mupen64plus-audio-android SHARED
        src/plugin.cpp
        src/osal_dynamiclib_unix.cpp
        src/AudioHandler.cpp
        src/RingBufferPool.cpp)
target_compile_definitions(mupen64plus-audio-android PUBLIC -D__SOFTFP__ -DSOUNDTOUCH_USE_NEON -DANDROID)

# Specify the libraries which our native library is dependent on, including Oboe
target_link_libraries(mupen64plus-audio-android log oboe::oboe soundtouch)

# Build our own native library
add_library (mupen64plus-audio-android-fp SHARED
        src/plugin.cpp
        src/osal_dynamiclib_unix.cpp
        src/AudioHandler.cpp
        src/RingBufferPool.cpp)
target_compile_definitions(mupen64plus-audio-android-fp PUBLIC -DFP_ENABLED -DSOUNDTOUCH_USE_NEON -DSOUNDTOUCH_FLOAT_SAMPLES -DANDROID)

# Specify the libraries which our native library is dependent on, including Oboe
target_link_libraries(mupen64plus-audio-android-fp log oboe::oboe soundtouch_fp)