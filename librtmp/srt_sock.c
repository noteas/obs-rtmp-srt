/*
 * srt_sock.c — 本 fork（obs-rtmp-srt）唯一的 SRT 传输层实现（ADR-0008）。
 *
 * 行为基准 = 官方 bvc-srt-stream.dll（E-24 §3/§7，libsrt 1.4.4；1.5.x 差异见 E-24 §9）：
 *   TRANSTYPE=live, CONNTIMEO=10000ms, PAYLOADSIZE=1456, LATENCY=200ms,
 *   TLPKTDROP=off, TSBPD=off, MAXBW=0x200000, LINGER=1s, RCVTIMEO=rcvtimeo_ms(0=阻塞)。
 * 不设 SRTO_STREAMID、不设密码：鉴权 100% 在 RTMP 层（E-24 §3/§6）。
 *
 * 跨平台：<srt/srt.h> 只在本文件出现；SRT 句柄以 RTMP 的 SOCKET 类型透传给 rtmp.c，
 * SRTSOCKET 的强制转换全部收敛在这里（Linux/Windows 同构，ADR-0008 / AGENTS 跨平台原则）。
 */

#include "srt_sock.h"
#include "rtmp_sys.h" /* INVALID_SOCKET / GetSockError 等 librtmp 内部定义 */
#include "log.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <srt/srt.h>

#ifndef _WIN32
#include <netdb.h>
#endif

static volatile int g_srt_started = 0;

static void sso_set_i(SRTSOCKET sock, SRT_SOCKOPT opt, const char *name, int val)
{
	int ret = srt_setsockopt(sock, 0, opt, &val, sizeof(val));
	if (ret == SRT_ERROR)
		/* 与官方一致：单项 sockopt 失败只记日志、不阻断连接（例如 PAYLOADSIZE=1456
		 * 在部分 libsrt 的 live 传输被拒，随后沿用默认 1316，官方亦如此）。 */
		RTMP_Log(RTMP_LOGWARNING, "[srt] set %s -> %d failed: %s", name, val, srt_getlasterror_str());
	else
		RTMP_Log(RTMP_LOGDEBUG, "[srt] set %s -> %d", name, val);
}

/* SRT 错误 → errno：让 librtmp 上层的 GetSockError() 判断（EAGAIN=超时轮询、
 * 其余=连接失效）与 TCP 路径同语义。 */
static void srt_fail_to_errno(void)
{
	switch (srt_getlasterror(NULL)) {
	case SRT_ETIMEOUT:
		errno = EAGAIN;
		break;
	case SRT_ECONNLOST:
	case SRT_ENOCONN:
	case SRT_EPEERERR:
	case SRT_EINVSOCK:
		errno = ECONNRESET;
		break;
	default:
		errno = EIO;
		break;
	}
}

static void apply_official_srt_options(SRTSOCKET sock, int maxbw, int rcvtimeo_ms)
{
	SRT_TRANSTYPE live = SRTT_LIVE;
	int v;

	sso_set_i(sock, SRTO_TRANSTYPE, "TRANSTYPE(live)", (int)live);
	sso_set_i(sock, SRTO_CONNTIMEO, "CONNTIMEO", 10000);
	sso_set_i(sock, SRTO_PAYLOADSIZE, "PAYLOADSIZE", 1456);
	sso_set_i(sock, SRTO_LATENCY, "LATENCY", 200);
	sso_set_i(sock, SRTO_TLPKTDROP, "TLPKTDROP", 0);
	sso_set_i(sock, SRTO_TSBPDMODE, "TSBPDMODE", 0);

	/* 官方 1.4.4 的 live 传输是字节流模式（E-24 §4：非消息边界依赖）；libsrt ≥1.5
	 * 的 live 默认 Message API=true（E-24 §9.2），显式回退流模式还原官方语义。 */
	sso_set_i(sock, SRTO_MESSAGEAPI, "MESSAGEAPI(stream)", 0);

	/* 官方固定 0x200000（BVC_SRT_DEFAULT_MAXBW）；0 = 沿用 libsrt 自动估计 */
	if (maxbw > 0)
		sso_set_i(sock, SRTO_MAXBW, "MAXBW", maxbw);

	/* SRTO_LINGER：libsrt 1.4.x 收 int、1.5.0 起收 struct linger（E-24 §9.1，
	 * 传错报 Bad parameters）——按实际链接的运行时版本选择。 */
	if (srt_getversion() >= 0x01050000) {
		struct linger lg;
		int ret;
		lg.l_onoff = 1;
		lg.l_linger = 1;
		ret = srt_setsockopt(sock, 0, SRTO_LINGER, &lg, (int)sizeof(lg));
		if (ret == SRT_ERROR)
			RTMP_Log(RTMP_LOGWARNING, "[srt] set LINGER(1s) failed: %s", srt_getlasterror_str());
	} else {
		v = 1;
		sso_set_i(sock, SRTO_LINGER, "LINGER(1s)", v);
	}

	sso_set_i(sock, SRTO_RCVTIMEO, "RCVTIMEO", rcvtimeo_ms); /* 0 = 阻塞（官方缺省） */
}

SOCKET RTMP_SRT_Open(RTMPSockBuf *sb, const char *host, int port, const struct sockaddr_storage *bindaddr,
                     int bindlen, int maxbw, int rcvtimeo_ms)
{
	char portstr[16];
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *ai;
	SOCKET out = INVALID_SOCKET;

	if (srt_startup() == SRT_ERROR) {
		RTMP_Log(RTMP_LOGERROR, "[srt] srt_startup failed: %s", srt_getlasterror_str());
		return INVALID_SOCKET;
	}
	g_srt_started = 1;

	snprintf(portstr, sizeof(portstr), "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /* SRT over UDP */

	if (getaddrinfo(host, portstr, &hints, &res) != 0) {
		RTMP_Log(RTMP_LOGERROR, "[srt] cannot resolve %s:%d", host, port);
		return INVALID_SOCKET;
	}

	for (ai = res; ai; ai = ai->ai_next) {
		SRTSOCKET sock = srt_create_socket();
		int eid;

		if (sock == SRT_INVALID_SOCK) {
			RTMP_Log(RTMP_LOGERROR, "[srt] srt_create_socket failed: %s", srt_getlasterror_str());
			break;
		}

		apply_official_srt_options(sock, maxbw, rcvtimeo_ms);

		if (bindaddr && bindlen > 0 && srt_bind(sock, (const struct sockaddr *)bindaddr, bindlen) == SRT_ERROR) {
			RTMP_Log(RTMP_LOGERROR, "[srt] bind local address failed: %s", srt_getlasterror_str());
			srt_close(sock);
			continue;
		}

		if (srt_connect(sock, ai->ai_addr, (int)ai->ai_addrlen) == SRT_ERROR) {
			srt_fail_to_errno();
			RTMP_Log(RTMP_LOGERROR, "[srt] connect %s:%d (%s) failed: %s", host, port, ai->ai_canonname ? ai->ai_canonname : "?",
			         srt_getlasterror_str());
			srt_close(sock);
			continue;
		}

		/* 每 socket 一个 level-triggered epoll：替代 TCP 的 FIONREAD 轮询
		 * （SRT 句柄不是内核 fd，ioctl 不可用）。 */
		eid = srt_epoll_create();
		if (eid >= 0) {
			const int ev = SRT_EPOLL_IN | SRT_EPOLL_ERR;
			if (srt_epoll_add_usock(eid, sock, &ev) == SRT_ERROR) {
				RTMP_Log(RTMP_LOGWARNING, "[srt] epoll add failed: %s", srt_getlasterror_str());
				srt_epoll_release(eid);
				eid = -1;
			}
		}

		sb->sb_srt_eid = eid;
		memcpy(&sb->sb_addr, ai->ai_addr,
		       sizeof(sb->sb_addr) < ai->ai_addrlen ? sizeof(sb->sb_addr) : ai->ai_addrlen);

		out = (SOCKET)(uintptr_t)sock;
		break;
	}

	freeaddrinfo(res);

	if (out == INVALID_SOCKET)
		RTMP_Log(RTMP_LOGERROR, "[srt] no usable address for %s:%d", host, port);

	return out;
}

int RTMP_SRT_Recv(SOCKET s, char *buf, int len)
{
	int n = srt_recv((SRTSOCKET)(uintptr_t)s, buf, len);
	if (n == SRT_ERROR) {
		srt_fail_to_errno();
		return -1;
	}
	return n;
}

int RTMP_SRT_Send(SOCKET s, const char *buf, int len)
{
	int sent = 0;

	while (sent < len) {
		int chunk = len - sent;
		/* 1316 = SRT live 默认 payload（E-24 §9.3：≥1.5 的 Message API 下单次发送上限）。
		 * 流模式下拆分不改变字节流内容，两种模式都安全。 */
		if (chunk > 1316)
			chunk = 1316;
		int n = srt_send((SRTSOCKET)(uintptr_t)s, buf + sent, chunk);
		if (n == SRT_ERROR) {
			srt_fail_to_errno();
			return -1;
		}
		sent += n;
	}
	return sent;
}

int RTMP_SRT_Close(RTMPSockBuf *sb)
{
	int ret = 0;

	if (sb->sb_srt_eid >= 0) {
		srt_epoll_release(sb->sb_srt_eid);
		sb->sb_srt_eid = -1;
	}
	if (sb->sb_socket != INVALID_SOCKET)
		ret = srt_close((SRTSOCKET)(uintptr_t)sb->sb_socket);
	return ret == SRT_ERROR ? -1 : 0;
}

int RTMP_SRT_DataPending(RTMPSockBuf *sb)
{
	SRT_EPOLL_EVENT ev;
	int n;

	if (sb->sb_srt_eid < 0)
		return 0;

	n = srt_epoll_uwait(sb->sb_srt_eid, &ev, 1, 0);
	if (n > 0 && ev.fd == (SRTSOCKET)(uintptr_t)sb->sb_socket && (ev.events & (SRT_EPOLL_IN | SRT_EPOLL_ERR)))
		return 1;
	return 0;
}

void RTMP_SRT_Cleanup(void)
{
	if (g_srt_started) {
		g_srt_started = 0;
		srt_cleanup();
	}
}
