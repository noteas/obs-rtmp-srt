# obs-rtmp-srt

OBS RTMP 推流栈的 fork。用途：作为 [obs-bilibili-stream](https://github.com/noteas/obs-bilibili-stream)
的 git submodule，实现 **B站私有 SRT 线路**（`rtmpsrt://`，= 完整 RTMP over SRT 传输，
对应官方直播姬 `bvc-srt-stream.dll` 的做法：RTMP 协议层原样、只把 TCP socket 换成 SRT）。

## 上游基线（逐字导入，未改动）

- 上游：`github.com/obsproject/obs-studio` tag **32.2.0**（commit `7546be7266dde276d82d4681fe1ab4fd8e32cf2b`）
- 导入路径：
  - `plugins/obs-outputs/librtmp/*` → `librtmp/`（含 LGPL-2.1 `COPYING`）
  - `plugins/obs-outputs/`：`rtmp-stream.c/h`、`rtmp-hevc.c/h`、`rtmp-av1.c/h`、
    `rtmp-windows.c`、`flv-mux.c/h`、`net-if.c/h`、`utils.h`、`rtmp-helpers.h`、`obs-output-ver.h` → 仓库根
  - `shared/happy-eyeballs/{happy-eyeballs.c,h}` → `happy-eyeballs/`
    （c-ares 血统，MIT；TCP 双栈连接路径编译所需，SRT 路径不经过它）
- 基线 commit 内以上文件与上游逐字节一致（`diff -r` 核对）。

## 修改策略

- 上游文件尽量 verbatim；所有改动位于基线之上的独立 commit，最小侵入、
  一律以 rtmpsrt 协议标志门控，**不改变 TCP/RTMP 既有行为**。
- 新增代码集中在新文件（SRT 传输层），上游文件里只留分支点。
- 上游同步：升级时以新基线 commit 重导，补丁 commit 逐个 rebase 复核。

## 补丁清单（基线之上）

口径来源 = obs-bilibili-stream 逆向证据 E-24/E-25/E-27（B站 SRT 入口 = UDP **1935**、
**无 streamid**、`LATENCY=200ms`、`PAYLOADSIZE=1456`、`MAXBW=2MiB/s`、`CONNTIMEO=10s`、
`LINGER=1s`、TSBPD/TLPKTDROP 关）。所有改动以 `RTMP_PROTOCOL_SRT`/`sb_srt` 门控，
TCP/RTMP 既有行为不变：

1. **SRT 传输层（新文件 `librtmp/srt_sock.c/h`）**：`RTMP_SRT_Open/Recv/Send/Close/DataPending/Cleanup`，
   唯一 `#include <srt/srt.h>` 处；E-24 §3 sockopt 序列 + `RTMP_SRT_DataPending`
   （每 socket 一个 SRT epoll，替代 TCP 的 `ioctl(FIONREAD)`）。
   libsrt 1.4/1.5 差异（`SRTO_LINGER` int↔struct、Message API、单次 send ≤1316B）在此收敛。
2. **`librtmp/rtmp.h`**：`RTMP_PROTOCOL_SRT = 0x40`、`RTMPSockBuf.sb_srt`/`sb_srt_eid`、
   `RTMP_LNK.srtMaxBandwidth`/`srtRcvTimeoutMs`。
3. **`librtmp/parseurl.c`**：认 `rtmpsrt://` scheme。
4. **`librtmp/rtmp.c`** 分支点：`RTMP_Connect` SRT 拨号、`RTMP_Connect0` 跳过 TCP 选项、
   `RTMPSockBuf_Fill/Send/Close` 换 srt 收发关、发送失败 linger 分支、
   `RTMPProtocolStringsLower[protocol]` 越界防御、`RTMP_Reset` 填官方默认值。
5. **`rtmp-stream.c`**：`handle_socket_read` FIONREAD→`RTMP_SRT_DataPending`、
   rtmpsrt 沿用 rtmps 先例关 `new_socket_loop`、output 身份 `bvc_srt_output` / `protocols=bvc_srt`、
   `init_send` 按官方公式写入 `srtMaxBandwidth`（E-24 §5，上游码率聚合值 ×1.58）。
6. **构建**：`CMakeLists.txt`（静态库 PIC，`USE_MBEDTLS CRYPTO`）+ `cmake/finders/`
   = OBS 32.2 官方 Find 模块：`FindMbedTLS`（上游逐字）、`Findjansson`（上游逐字）、
   `FindLibsrt`（上游逐字 + Debian 变体扩展：pkg-config 名 `srt`/`haisrt`、库名 `srt-gnutls`）。
   依赖解析与上游 obs-outputs 同款：`find_package(MbedTLS/ZLIB/jansson/Libsrt REQUIRED)`
   → imported targets（`MbedTLS::mbedtls`/`ZLIB::ZLIB`/`jansson::jansson`/`Libsrt::Libsrt`）。
   静态/动态 = OBS 插件生态惯例：本库 STATIC（只进插件 MODULE，从不单独发布）；
   第三方一律不打包，Linux/flatpak 链发行版 .so，Windows 链 obs-deps 导入库
   （mbedcrypto 为静态 ⇒ bcrypt 与 jansson CRT 链接选项随之上游同款）。
   crypto 后端 = mbedTLS，与官方 OBS 一致；TLS 本身 rtmpsrt 用不到（SSL 位不置），
   握手 HMAC-SHA256 复用 librtmp 原生 `USE_MBEDTLS` 路径。

## 许可证

- 整体 GPL-2.0-or-later（OBS 各文件头）；`librtmp/` 为 LGPL-2.1（见 `librtmp/COPYING`）；
  `happy-eyeballs/` 为 MIT（文件头）。
