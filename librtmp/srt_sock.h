/*
 * srt_sock.h — 本 fork（obs-rtmp-srt）唯一的 SRT 传输层内部头（ADR-0008）。
 *
 * 设计 = 官方 bvc-srt-stream.dll 同款：RTMP 协议层原样，socket 层换成 SRT
 * （逆向证据 E-24）。除 srt_sock.c 外任何文件不得 include <srt/srt.h>。
 */

#ifndef OBS_RTMP_SRT_SOCK_H
#define OBS_RTMP_SRT_SOCK_H

#include "rtmp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 官方 bvc-srt-stream 的 SRTO_MAXBW 固定值（E-24 §3；settings key srt_max_bandwidth 可覆盖） */
#define BVC_SRT_DEFAULT_MAXBW 0x200000

/* 按 E-24 §3 的官方参数序列创建并连接一条 SRT（UDP）链路。
 * 成功返回 SRTSOCKET（以 RTMP 的 SOCKET 类型透传），并填 sb->sb_srt_eid / sb->sb_addr；
 * 失败返回 INVALID_SOCKET（srt/系统错误已记日志）。
 * bindaddr/bindlen = 可空本地绑定（对应 OBS bind_ip 设置，官方同款 bind→connect）；
 * maxbw = SRTO_MAXBW（0 = libsrt 自动；官方恒设 0x200000，见 BVC_SRT_DEFAULT_MAXBW）；
 * rcvtimeo_ms = SRTO_RCVTIMEO，0 = 阻塞（官方 key 缺省语义，E-24 §5）。 */
SOCKET RTMP_SRT_Open(RTMPSockBuf *sb, const char *host, int port, const struct sockaddr_storage *bindaddr,
                     int bindlen, int maxbw, int rcvtimeo_ms);

/* 收发/关闭：返回语义与 recv/send/closesocket 对齐（失败置 errno 并返回 -1；
 * 超时 → EAGAIN；对端关闭/丢失 → ECONNRESET）。 */
int RTMP_SRT_Recv(SOCKET s, char *buf, int len);
int RTMP_SRT_Send(SOCKET s, const char *buf, int len);
int RTMP_SRT_Close(RTMPSockBuf *sb);

/* 可读性轮询（替代 FIONREAD ioctl；SRT 句柄不是内核 fd）：>0 可读/异常，0 无。 */
int RTMP_SRT_DataPending(RTMPSockBuf *sb);

/* 模块卸载时调用；与首次连接时的 srt_startup 配平（未连接过则不动作）。 */
void RTMP_SRT_Cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* OBS_RTMP_SRT_SOCK_H */
