/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Xymon network test tool - reusable single-shot UDP query transport: one
 * datagram out, one reply in, with a timeout. Concurrency belongs in the conn_*
 * engine (lib/tcplib.c), not here - keep this a primitive. */

#ifndef __UDPQUERY_H__
#define __UDPQUERY_H__

#include <stddef.h>
#include <sys/time.h>

/* Tuning for udp_query_opt(); NULL selects the defaults. */
typedef struct {
	long        timeout_us;	/* sub-second timeout; 0 = use the whole-second arg */
	unsigned    flags;	/* UDPQ_* below */
	const char *srcip;	/* numeric source to bind(), server's family (host@srcip) */
} udp_query_opts;

#define UDPQ_DETECT_TRUNC 0x01	/* fail instead of silently truncating an oversized reply */

/*
 * Send reqlen bytes to ip:port (numeric literal), wait up to timeout seconds for
 * one reply into resp[respsz]. Returns bytes received (>=0), or -1 with *errstr.
 * The connect()ed socket drops off-source replies (single-exchange only).
 * sent_tv/recv_tv, if non-NULL, are stamped around send/recv and a mid-query
 * wall-clock step is then rejected.
 */
int udp_query_opt(const char *ip, const char *port,
		  const void *req, size_t reqlen,
		  void *resp, size_t respsz, int timeout,
		  const udp_query_opts *opts,
		  struct timeval *sent_tv, struct timeval *recv_tv,
		  const char **errstr);

#endif
