/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*----------------------------------------------------------------------------*/
/* Xymon network test tool - reusable single-shot UDP query transport.        */
/* See udpquery.h for the contract.                                           */
/*----------------------------------------------------------------------------*/

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>		/* struct iovec for recvmsg() */
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>		/* Needed on some platforms for select() */
#endif
#include <netinet/in.h>		/* IPPROTO_UDP */
#include <netdb.h>

#include "udpquery.h"

/* Reject the timestamps if the wall and monotonic clocks disagree by more than
 * this over the round trip: the wall clock was stepped mid-query. Above jitter,
 * below a real clock step (~128 ms for a time daemon). */
#define UDP_CLOCK_STEP_US 50000L

/* Monotonic clock for timeout accounting, immune to wall-clock steps; gettimeofday()
 * fallback. The returned send/recv timestamps stay wall-clock. */
static void mono_gettime(struct timeval *tv)
{
#ifdef CLOCK_MONOTONIC
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		tv->tv_sec  = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
		return;
	}
#endif
	gettimeofday(tv, NULL);
}

/* Sample the wall clock (*wall) paired to the monotonic clock (*mono), bracketing
 * the wall read between two monotonic reads. *mono is the bracket midpoint - the
 * monotonic instant the wall stamp best corresponds to - and *gap_us is the bracket
 * width, normally sub-microsecond but large if the thread was preempted between the
 * two reads. The clock-step check folds gap_us into its tolerance, so a scheduler
 * pause inside this window widens the allowance instead of being misread as a wall
 * step: the step skew is (gap_send - gap_recv), bounded by (gap_send+gap_recv)/2. */
static void sample_clocks(struct timeval *wall, struct timeval *mono, long *gap_us)
{
	struct timeval m0, m1;

	mono_gettime(&m0);
	gettimeofday(wall, NULL);
	mono_gettime(&m1);

	*gap_us = (m1.tv_sec - m0.tv_sec) * 1000000L + (m1.tv_usec - m0.tv_usec);
	if (*gap_us < 0) *gap_us = 0;	/* non-monotonic fallback: no usable bound */
	mono->tv_sec  = m0.tv_sec;
	mono->tv_usec = m0.tv_usec + *gap_us / 2;
	while (mono->tv_usec >= 1000000L) { mono->tv_usec -= 1000000L; mono->tv_sec++; }
}

int udp_query_opt(const char *ip, const char *port,
		  const void *req, size_t reqlen,
		  void *resp, size_t respsz, int timeout,
		  const udp_query_opts *opts,
		  struct timeval *sent_tv, struct timeval *recv_tv,
		  const char **errstr)
{
	struct addrinfo hints, *res, *ai;
	const char *err = "no response from server";
	int gai, result = -1;
	int detect_trunc = (opts && (opts->flags & UDPQ_DETECT_TRUNC));
	int want_ts = (sent_tv || recv_tv);	/* skip all clock sampling if no caller wants timestamps */
	long budget_us;

	/* timeout_us overrides the whole-second timeout; else floor at 1 s. */
	if (opts && (opts->timeout_us > 0)) {
		budget_us = opts->timeout_us;
	}
	else {
		if (timeout < 1) timeout = 1;
		budget_us = (long)timeout * 1000000L;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags    = AI_NUMERICHOST;	/* ip is an already-resolved literal */

	gai = getaddrinfo(ip, port, &hints, &res);
	if (gai != 0) {
		if (errstr) *errstr = gai_strerror(gai);
		return -1;
	}

	for (ai = res; (ai && (result < 0)); ai = ai->ai_next) {
		int fd;
		ssize_t n;	/* holds send()/recvmsg() byte counts; select()'s int return promotes cleanly */
		fd_set rfds;
		struct timeval tmo, ts, tr, deadline, now;
		struct timeval mono_send, mono_recv;
		struct msghdr msg;
		struct iovec  iov;
		long wall_us, mono_us, skew_us, gap_send = 0, gap_recv = 0;

		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) { err = "socket() failed"; continue; }

		/* Bind the source address before connect() (multihomed/ACL hosts),
		 * resolved in the server's family so a family mismatch fails cleanly
		 * instead of being ignored; ephemeral source port. */
		if (opts && opts->srcip && *opts->srcip) {
			struct addrinfo bhints, *bres;

			memset(&bhints, 0, sizeof(bhints));
			bhints.ai_family   = ai->ai_family;
			bhints.ai_socktype = ai->ai_socktype;
			bhints.ai_protocol = ai->ai_protocol;
			bhints.ai_flags    = AI_NUMERICHOST | AI_PASSIVE;
			if (getaddrinfo(opts->srcip, NULL, &bhints, &bres) != 0) {
				err = "source address invalid"; close(fd); continue;
			}
			if (bind(fd, bres->ai_addr, bres->ai_addrlen) != 0) {
				err = "bind to source address failed";
				freeaddrinfo(bres); close(fd); continue;
			}
			freeaddrinfo(bres);
		}

		/* connect() so the kernel drops replies from any other address/port
		 * (stray/spoofed). */
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
			err = "connect() failed";
			close(fd);
			continue;
		}

		/* Stamp ts right before the send() that actually leaves: an EINTR retry
		 * delays the packet, and the timestamp must track the real send. */
		do {
			if (want_ts) sample_clocks(&ts, &mono_send, &gap_send);
			n = send(fd, req, reqlen, 0);
		} while ((n < 0) && (errno == EINTR));
		if (n != (ssize_t)reqlen) {
			err = "send() failed";
			close(fd);
			continue;
		}

		/* Wait on the monotonic deadline, retrying across EINTR without letting
		 * a signal storm extend the wait past the timeout. */
		mono_gettime(&deadline);
		deadline.tv_sec  += budget_us / 1000000L;
		deadline.tv_usec += budget_us % 1000000L;
		if (deadline.tv_usec >= 1000000L) { deadline.tv_usec -= 1000000L; deadline.tv_sec++; }
		for (;;) {
			mono_gettime(&now);
			tmo.tv_sec  = deadline.tv_sec  - now.tv_sec;
			tmo.tv_usec = deadline.tv_usec - now.tv_usec;
			if (tmo.tv_usec < 0) { tmo.tv_usec += 1000000; tmo.tv_sec--; }
			if (tmo.tv_sec  < 0) { tmo.tv_sec = 0; tmo.tv_usec = 0; }

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);
			n = select(fd + 1, &rfds, NULL, NULL, &tmo);
			if ((n < 0) && (errno == EINTR)) continue;
			break;
		}
		if (n <= 0) {
			err = (n == 0) ? "timed out waiting for response" : "select() failed";
			close(fd);
			continue;
		}

		/* recvmsg (not recv) so msg_flags reports MSG_TRUNC when the datagram
		 * was larger than respsz - otherwise truncation is silent. */
		memset(&msg, 0, sizeof(msg));
		iov.iov_base = resp; iov.iov_len = respsz;
		msg.msg_iov = &iov; msg.msg_iovlen = 1;
		do { n = recvmsg(fd, &msg, 0); } while ((n < 0) && (errno == EINTR));
		if (want_ts) sample_clocks(&tr, &mono_recv, &gap_recv);
		close(fd);
		if (n < 0) { err = "recv() failed"; continue; }
		if (detect_trunc && (msg.msg_flags & MSG_TRUNC)) { err = "response truncated"; continue; }

		/* Wall-clock-step check (see UDP_CLOCK_STEP_US) only when the caller
		 * wants timestamps; a bytes-only probe (NULL) isn't failed by it. */
		if (want_ts) {
			wall_us = (tr.tv_sec - ts.tv_sec) * 1000000L + (tr.tv_usec - ts.tv_usec);
			mono_us = (mono_recv.tv_sec - mono_send.tv_sec) * 1000000L
				+ (mono_recv.tv_usec - mono_send.tv_usec);
			skew_us = wall_us - mono_us;
			if (skew_us < 0) skew_us = -skew_us;
			/* Allow the fixed step threshold plus the measurement uncertainty: the
			 * pairing error is bounded by (gap_send + gap_recv)/2, so a preemption
			 * between a wall/mono pair widens the allowance rather than masquerading
			 * as a clock step (see sample_clocks). */
			if (skew_us > UDP_CLOCK_STEP_US + (gap_send + gap_recv) / 2) {
				err = "local clock stepped during query";
				continue;
			}
			if (sent_tv) *sent_tv = ts;
			if (recv_tv) *recv_tv = tr;
		}
		result = n;
	}

	freeaddrinfo(res);

	if ((result < 0) && errstr) *errstr = err;
	return result;
}
