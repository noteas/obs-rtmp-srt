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

实施中（口径来源 = obs-bilibili-stream 的逆向证据 E-24：B站 SRT 入口 = UDP **1935**、
**无 streamid**、`LATENCY=200ms`、`PAYLOADSIZE=1456`、`MAXBW=2MiB/s`、`CONNTIMEO=10s`、
`LINGER=1s`、TSBPD/TLPKTDROP 关）：

1. SRT 传输层（新文件 `srt_socket.c/h`）：`RTMP_Connect` 的 SRT 分支、
   `RTMPSockBuf_Fill/Send/Close` 收发分支、`RTMP_Connect0` socket 选项分支、
   `rtmp-stream.c` 两处 `ioctlsocket` 分支。
2. `rtmpsrt://` scheme（`parseurl.c`）+ output 身份改为 `bvc_srt_output`（`rtmp-stream.c`）。

## 许可证

- 整体 GPL-2.0-or-later（OBS 各文件头）；`librtmp/` 为 LGPL-2.1（见 `librtmp/COPYING`）；
  `happy-eyeballs/` 为 MIT（文件头）。
