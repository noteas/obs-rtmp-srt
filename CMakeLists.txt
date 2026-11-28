# obs-rtmp-srt — own fork of the OBS 32.2 RTMP output stack with the socket
# layer swapped to libsrt (ADR-0008). Built as a static library and linked into
# the obs-bilibili-stream plugin (main repo adds this directory via
# add_subdirectory; dependencies resolve there already).
#
# Dependency wiring mirrors upstream plugins/obs-outputs/CMakeLists.txt verbatim
# (USE_MBEDTLS CRYPTO, MbedTLS::mbedtls, ZLIB, jansson) — see README.
#
# 静态/动态口径(OBS 插件生态惯例):
# - 本库 = STATIC,只作为插件 MODULE 的内部组成(从不单独发布),PIC 强制开启;
# - 第三方依赖全部经 find_package 解析成 imported target,由模块/config 包决定
#   后端形态:Linux 与 flatpak 解析为发行版共享库(.so,插件不打包第三方,
#   DT_NEEDED 交给系统/OBS 运行时);Windows 解析为 obs-deps 的导入库/静态库
#   (srt.dll 等由官方 OBS 安装包在 obs64 同目录提供)。
cmake_minimum_required(VERSION 3.16...3.30)

project(obs-rtmp-srt LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/finders")

# When built standalone (not embedded in the plugin) libobs must be found here;
# inside the plugin build the parent already provides OBS::libobs.
if(NOT TARGET OBS::libobs)
  find_package(libobs REQUIRED)
endif()

# OBS 32.2 官方 Find 模块(cmake/finders/,obs-outputs 同款):
#   MbedTLS::mbedtls / ZLIB::ZLIB / jansson::jansson / Libsrt::Libsrt。
# PREFER_CONFIG 只包住 MbedTLS(与上游 obs-outputs 逐字一致):Windows 走
# obs-deps 自带的 MbedTLS config 包(带逐配置导入库与正确 CRT 运行期链接),
# Linux/flatpak 无 config 包则自然落回本目录的 FindMbedTLS 模块。
# SRT 是 fork 存在的全部理由,缺失一律硬失败(FindLibsrt 的失败信息含两端修法)。
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
find_package(MbedTLS REQUIRED)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG FALSE)
find_package(ZLIB REQUIRED)
find_package(jansson REQUIRED)
find_package(Libsrt REQUIRED)

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

# srt_sock.c is the only consumer of <srt/srt.h> (ADR-0008); the include dir
# arrives via Libsrt::Libsrt's INTERFACE and stays PRIVATE, so libsrt never
# leaks into the plugin's own compilation.
target_include_directories(obs-rtmp-srt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/librtmp"
                                                "${CMAKE_CURRENT_SOURCE_DIR}/happy-eyeballs"
)

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
          Libsrt::Libsrt
          $<$<PLATFORM_ID:Windows>:ws2_32>
          $<$<PLATFORM_ID:Windows>:winmm>
          $<$<PLATFORM_ID:Windows>:iphlpapi>
          $<$<PLATFORM_ID:Windows>:crypt32>
          # mbedcrypto(obs-deps 静态库)的 entropy_poll 调 BCryptGenRandom，
          # 最终可执行/插件 DLL 链接时必须带上 bcrypt（2026-11-27 CI 实证 LNK2001）
          $<$<PLATFORM_ID:Windows>:bcrypt>
)

# 上游 obs-outputs 同款("Remove once jansson has been fixed on obs-deps"):
# obs-deps 的 jansson 静态库混链了非统一 CRT，MSVC 需要忽略 CRT 库冲突警告。
# 静态库上声明 ⇒ 作为 usage requirement 传播给最终链接的插件 DLL。
target_link_options(
  obs-rtmp-srt
  PRIVATE $<$<PLATFORM_ID:Windows>:/IGNORE:4098> $<$<AND:$<PLATFORM_ID:Windows>,$<CONFIG:DEBUG>>:/NODEFAULTLIB:MSVCRT>
)
