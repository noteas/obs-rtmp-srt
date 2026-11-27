# obs-rtmp-srt — own fork of the OBS 32.2 RTMP output stack with the socket
# layer swapped to libsrt (ADR-0008). Built as a static library and linked into
# the obs-bilibili-stream plugin (main repo adds this directory via
# add_subdirectory; dependencies resolve there already).
#
# Dependency wiring mirrors upstream plugins/obs-outputs/CMakeLists.txt verbatim
# (USE_MBEDTLS CRYPTO, MbedTLS::mbedtls, ZLIB, jansson) — see README.
cmake_minimum_required(VERSION 3.16...3.30)

project(obs-rtmp-srt LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/finders")

# When built standalone (not embedded in the plugin) libobs must be found here;
# inside the plugin build the parent already provides OBS::libobs.
if(NOT TARGET OBS::libobs)
  find_package(libobs REQUIRED)
endif()

find_package(MbedTLS REQUIRED)
find_package(ZLIB REQUIRED)

if(NOT TARGET jansson::jansson)
  find_package(jansson CONFIG QUIET)
endif()
if(NOT TARGET jansson::jansson)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)
  add_library(jansson::jansson ALIAS PkgConfig::JANSSON)
endif()

# libsrt: pkg-config first, classic find as fallback (names cover the Ubuntu
# "srt-gnutls" flavor and the obs-deps "srt" import lib on Windows). SRT is the
# entire reason this fork exists -> hard fail when absent (both platforms).
find_package(PkgConfig QUIET)
set(BVC_SRT_LIBRARIES)
set(BVC_SRT_INCLUDE_DIRS)
if(PkgConfig_FOUND)
  pkg_check_modules(BVC_SRT QUIET srt)
endif()
if(BVC_SRT_FOUND)
  set(BVC_SRT_LIBRARIES ${BVC_SRT_LINK_LIBRARIES})
  set(BVC_SRT_INCLUDE_DIRS ${BVC_SRT_INCLUDE_DIRS})
else()
  find_path(BVC_SRT_INCLUDE_DIR NAMES srt/srt.h)
  find_library(BVC_SRT_LIBRARY NAMES srt srt-gnutls)
  if(BVC_SRT_INCLUDE_DIR AND BVC_SRT_LIBRARY)
    set(BVC_SRT_LIBRARIES ${BVC_SRT_LIBRARY})
    set(BVC_SRT_INCLUDE_DIRS ${BVC_SRT_INCLUDE_DIR})
  endif()
endif()
if(NOT BVC_SRT_LIBRARIES)
  message(
    FATAL_ERROR
    "libsrt not found. On Linux install libsrt-gnutls-dev (or pass -DCMAKE_PREFIX_PATH to a prefix "
    "containing it); on Windows ensure obs-deps (w32-srt) is part of CMAKE_PREFIX_PATH."
  )
endif()

add_library(obs-rtmp-srt STATIC)

# 链入插件 MODULE（共享库）必须 PIC
set_target_properties(obs-rtmp-srt PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(
  obs-rtmp-srt
  PRIVATE
    happy-eyeballs/happy-eyeballs.c
    flv-mux.c
    librtmp/amf.c
    librtmp/cencode.c
    librtmp/hashswf.c
    librtmp/log.c
    librtmp/md5.c
    librtmp/parseurl.c
    librtmp/rtmp.c
    librtmp/srt_sock.c
    net-if.c
    rtmp-av1.c
    rtmp-stream.c
    $<$<PLATFORM_ID:Windows>:rtmp-windows.c>
    $<$<BOOL:${ENABLE_HEVC}>:rtmp-hevc.c>
)

# srt_sock.c is the only consumer of <srt/srt.h> (ADR-0008); keeping the include
# dir PRIVATE stops libsrt leaking into the plugin's own compilation.
target_include_directories(obs-rtmp-srt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/librtmp"
                                                "${CMAKE_CURRENT_SOURCE_DIR}/happy-eyeballs"
                                                ${BVC_SRT_INCLUDE_DIRS})

target_compile_definitions(
  obs-rtmp-srt
  PRIVATE USE_MBEDTLS CRYPTO $<$<BOOL:${ENABLE_HEVC}>:ENABLE_HEVC>
)

target_link_libraries(
  obs-rtmp-srt
  PRIVATE OBS::libobs
          MbedTLS::mbedtls
          ZLIB::ZLIB
          jansson::jansson
          ${BVC_SRT_LIBRARIES}
          $<$<PLATFORM_ID:Windows>:ws2_32>
          $<$<PLATFORM_ID:Windows>:winmm>
          $<$<PLATFORM_ID:Windows>:iphlpapi>
          $<$<PLATFORM_ID:Windows>:crypt32>
)
