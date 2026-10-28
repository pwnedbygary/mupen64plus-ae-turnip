cmake_minimum_required(VERSION 3.22)

project( Miniupnp-bridge )

add_definitions(-DANDROID)

# miniupnpc and libnatpmp (see VENDOR.md in ndkLibs/miniupnpc and ndkLibs/libnatpmp) keep the
# flags of the 2021 prebuilt libraries that they replaced. They are C, so the module's C++
# flags do not reach them.
set( MINIUPNPC_DIR "${ANDROID_LIB_PATH}/miniupnpc" )
set( LIBNATPMP_DIR "${ANDROID_LIB_PATH}/libnatpmp" )
set( NET_LIB_OPTIONS -O3 -fsigned-char -fno-strict-aliasing -Wall -Wno-multichar )
if( ANDROID_ABI STREQUAL "armeabi-v7a" )
    list( APPEND NET_LIB_OPTIONS -marm )
endif()

add_library( miniupnp-build SHARED
        ${MINIUPNPC_DIR}/src/igd_desc_parse.c
        ${MINIUPNPC_DIR}/src/miniupnpc.c
        ${MINIUPNPC_DIR}/src/minixml.c
        ${MINIUPNPC_DIR}/src/minisoap.c
        ${MINIUPNPC_DIR}/src/minissdpc.c
        ${MINIUPNPC_DIR}/src/miniwget.c
        ${MINIUPNPC_DIR}/src/upnpcommands.c
        ${MINIUPNPC_DIR}/src/upnpdev.c
        ${MINIUPNPC_DIR}/src/upnpreplyparse.c
        ${MINIUPNPC_DIR}/src/upnperrors.c
        ${MINIUPNPC_DIR}/src/connecthostport.c
        ${MINIUPNPC_DIR}/src/portlistingparse.c
        ${MINIUPNPC_DIR}/src/receivedata.c
        ${MINIUPNPC_DIR}/src/addr_is_reserved.c )
target_include_directories( miniupnp-build
        PRIVATE ${ANDROID_LIB_PATH}/config/miniupnpc
        PUBLIC ${MINIUPNPC_DIR}/include )
target_compile_definitions( miniupnp-build PRIVATE __STDC_CONSTANT_MACROS )
target_compile_options( miniupnp-build PRIVATE ${NET_LIB_OPTIONS} )

add_library( natpmp SHARED
        ${LIBNATPMP_DIR}/natpmp.c
        ${LIBNATPMP_DIR}/getgateway.c )
target_compile_definitions( natpmp PRIVATE __STDC_CONSTANT_MACROS )
target_compile_options( natpmp PRIVATE ${NET_LIB_OPTIONS} )

SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -O3 -DWITH_UPNP")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_library (miniupnp-bridge SHARED
        src/PortManager.cpp
        src/PortManagerNatPmp.cpp)

target_compile_definitions(miniupnp-bridge PUBLIC -DANDROID)
target_include_directories(miniupnp-bridge PRIVATE ${ANDROID_LIB_PATH})

# Specify the libraries which our native library is dependent on, including Oboe
target_link_libraries(miniupnp-bridge log miniupnp-build natpmp)
