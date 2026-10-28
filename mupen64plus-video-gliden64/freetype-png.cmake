# libfreetype.so for GLideN64's on-screen text, from ndkLibs/freetype, with the libpng of
# ndkLibs/png inside it (ndkLibs/freetype/VENDOR.md and ndkLibs/png/VENDOR.md explain the files
# and flags). Included by upstream/src/CMakeLists.txt. GLideN64 links libfreetype.so before
# GLideNHQ, which links png_static, so GLideN64's png_* calls bind to the copy in libfreetype.so.
# The targets also get GLideN64's directory include folders: BEFORE puts their own ahead. The
# target options come after the module's -O3 -ffast-math, and override them.

set( FREETYPE_DIR "${ANDROID_LIB_PATH}/freetype" )
set( PNG_DIR "${ANDROID_LIB_PATH}/png" )
set( PNG_CONFIG_DIR "${ANDROID_LIB_PATH}/config/png" )

set( PNG_SOURCES png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c
        pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c )
if( ANDROID_ABI STREQUAL "armeabi-v7a" OR ANDROID_ABI STREQUAL "arm64-v8a" )
    list( APPEND PNG_SOURCES arm/arm_init.c arm/filter_neon_intrinsics.c
            arm/palette_neon_intrinsics.c )
endif()
list( TRANSFORM PNG_SOURCES PREPEND "${PNG_DIR}/" )
add_library( png_static STATIC ${PNG_SOURCES} )
set_target_properties( png_static PROPERTIES POSITION_INDEPENDENT_CODE ON )
target_include_directories( png_static BEFORE PRIVATE "${PNG_DIR}" "${PNG_CONFIG_DIR}" )
if( ANDROID_ABI STREQUAL "armeabi-v7a" )
    target_compile_definitions( png_static PRIVATE PNG_ARM_NEON_OPT=2 PNG_ARM_NEON_API_SUPPORTED )
    target_compile_options( png_static PRIVATE -mfpu=neon )
endif()
target_compile_options( png_static PRIVATE -O3 -fno-fast-math )
target_link_libraries( png_static INTERFACE z )

set( FREETYPE_SOURCES base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbbox.c
        base/ftbitmap.c base/ftfstype.c base/ftgasp.c base/ftglyph.c base/ftmm.c
        base/ftstroke.c base/fttype1.c autofit/autofit.c truetype/truetype.c cff/cff.c
        psaux/psaux.c psnames/psnames.c pshinter/pshinter.c sfnt/sfnt.c smooth/smooth.c
        raster/raster.c gzip/ftgzip.c )
list( TRANSFORM FREETYPE_SOURCES PREPEND "${FREETYPE_DIR}/src/" )
add_library( freetype SHARED ${FREETYPE_SOURCES} )
target_include_directories( freetype BEFORE PRIVATE "${ANDROID_LIB_PATH}/config"
        "${FREETYPE_DIR}/include" "${PNG_DIR}" "${PNG_CONFIG_DIR}" )
target_compile_definitions( freetype PRIVATE PIC DARWIN_NO_CARBON FT2_BUILD_LIBRARY
        "FT_CONFIG_OPTIONS_H=<freetype-android/ftoption.h>"
        "FT_CONFIG_MODULES_H=<freetype-android/ftmodule.h>" )
target_compile_options( freetype PRIVATE -W -Wall -O2 -fno-fast-math
        $<$<STREQUAL:${ANDROID_ABI},armeabi-v7a>:-marm> )
target_link_libraries( freetype PRIVATE png_static z )
