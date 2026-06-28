/*----------------------------------------------------------------------------*/
/* Xymon network test tool - internal SNTP probe.                             */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*                                                                            */
/* In-process SNTP (RFC 4330/5905) client for the "ntp" test: one 48-byte UDP */
/* exchange on port 123, no fork and no external tool. ntp_build_request and  */
/* ntp_eval are pure (no socket) so tests/network can drive them; the socket  */
/* itself is the reusable udp_query_opt() transport. The banner places the offset */
/* just before " +/- " so the offset parser added with the threshold/RRD support */
/* can read it back out of the status text.                                   */
/*                                                                            */
/* run_ntp_service() probes hosts serially via the blocking udp_query_opt() (see  */
/* udpquery.h for why that transport stays a thin one-shot, and the conn_*    */
/* engine that will later make it concurrent). Two invariants must hold for   */
/* whatever transport drives these functions:                                 */
/*   1. T1/T4 are wall-clock stamps taken right at send()/recv();             */
/*   2. the mid-query wall-clock-step rejection (see udpquery.c) is kept.     */
/*----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
/* getrandom() lives in <sys/random.h> (glibc >= 2.25, 2017). Probe for the header
 * with __has_include so pre-2.25 Linux (e.g. CentOS/RHEL 7, glibc 2.17) still
 * builds: when it is absent GRND_NONBLOCK stays undefined and ntp_nonce() simply
 * falls through to /dev/urandom. __has_include is itself guarded for pre-C11
 * preprocessors (e.g. RHEL 7's stock gcc 4.8), which take the same fallback. */
#if defined(__linux__) && defined(__has_include)
#  if __has_include(<sys/random.h>)
#    include <sys/random.h>		/* getrandom() (preferred nonce source) */
#  endif
#endif

#include "udpquery.h"

/* Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01) */
#define NTP_UNIX_DELTA 2208988800UL
#define NTP_PKTSIZE    48

/* Timestamps are raw 64-bit NTP values (32.32 fixed point); every delta is taken
 * as (int64_t)(a - b), so the 2^64 wrap is exactly the era rollover (next
 * 2036-02-07) and a close pair yields the right signed delta across it, with none
 * of the float cancellation a ~3.9e9-magnitude double would suffer. Seconds are
 * produced only at the end, by dividing by 2^32. */
#define NTP_FP_SCALE 4294967296.0		/* 2^32: fixed-point units per second */

/* Largest negative round-trip delay tolerated before the sample is treated as
 * bogus: real negatives are sub-millisecond timestamp-granularity noise, so
 * 1 ms is well above that yet far below a broken/hostile server's skew. */
#define NTP_DELAY_EPSILON_FP 4294967LL		/* ~0.001 s in 2^-32 s units */

/* RFC 5905 root distance (Lambda = rootdelay/2 + rootdisp) bounds how wrong the
 * server's own time may be; above this it is unusable as a source even though it
 * answered. 1 s is the RFC 5905 MAXDIST default - generous enough that a genuine
 * WAN server (root distance well under 100 ms) never trips it. */
#define NTP_MAXROOTDIST   1.0
/* A sane server sets its reference clock before transmitting, so reftime must
 * precede T3 (small slop for granularity); beyond it the clock is in its own
 * future - broken. The compare is server-stamp vs server-stamp, so it is
 * independent of our local clock being wrong (what the probe exists to detect). */
#define NTP_REFTIME_SLOP_FP ((int64_t)1 << 32)	/* 1.0 s in 2^-32 s units */

/* Retransmission policy. udp_query_opt() is single-shot, so without retries a lost
 * datagram reports a reachable server as down. A 2 s attempt is ~20x a worst-case
 * NTP RTT; 3 attempts survive two drops and bound a dead host to ~TRIES*TRY_SEC. */
#define NTP_PROBE_TRIES    3
#define NTP_PROBE_TRY_SEC  2

/* ntp_eval() return codes (0 = success); negative = no usable sample */
#define NTP_OK            0
#define NTP_ERR_SHORT    (-1)	/* truncated response                        */
#define NTP_ERR_MODE     (-2)	/* not a server/broadcast reply              */
#define NTP_ERR_LEAP     (-3)	/* leap indicator = 3: server unsynchronised */
#define NTP_ERR_STRATUM  (-4)	/* stratum > 15: out of range                */
#define NTP_ERR_ORIGIN   (-5)	/* originate timestamp != our transmit       */
#define NTP_ERR_XMTZERO  (-6)	/* transmit timestamp zero                   */
#define NTP_ERR_RXZERO   (-7)	/* receive timestamp zero                    */
#define NTP_ERR_BADTIME  (-8)	/* timestamps inconsistent (T3<T2/neg delay) */
#define NTP_ERR_ROOTDIST (-9)	/* root distance too large: server too uncertain */
#define NTP_ERR_REFTIME  (-10)	/* reference timestamp in the server's own future */
#define NTP_ERR_KOD      (-11)	/* stratum 0 Kiss-o'-Death (refid carries the code) */
#define NTP_ERR_VERSION  (-12)	/* reply version not in the RFC 5905 range (1..4) */

static uint32_t ntp_be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void ntp_put_be32(unsigned char *p, uint32_t v)
{
	p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff;
	p[2] = (v >> 8)  & 0xff; p[3] =  v        & 0xff;
}

/* A 64-bit NTP timestamp (seconds.fraction, 32.32 fixed point) straight from the
 * wire as a raw uint64 in 2^-32 s units (see NTP_FP_SCALE: deltas of these are
 * taken with (int64_t)(a - b), so the era wrap is handled for free). */
static uint64_t ntp64_wire(const unsigned char *p)
{
	return ((uint64_t)ntp_be32(p) << 32) | (uint64_t)ntp_be32(p + 4);
}

/* A wall-clock struct timeval as the same raw 64-bit NTP timestamp. The seconds
 * land in the low 32 bits (current era); the microseconds are scaled to a 2^-32 s
 * fraction with integer math, so no precision is lost before the delta is taken. */
static uint64_t ntp64_tv(const struct timeval *tv)
{
	uint64_t sec  = (uint32_t)(tv->tv_sec + NTP_UNIX_DELTA);
	uint64_t frac = ((uint64_t)tv->tv_usec << 32) / 1000000UL;

	return (sec << 32) | frac;
}

/* gettimeofday as a double "NTP seconds" value (since 1900), plus the matching
 * 32.32 second/fraction fields. */
static double ntp_now(uint32_t *sec, uint32_t *frac)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	*sec  = (uint32_t)(tv.tv_sec + NTP_UNIX_DELTA);
	*frac = (uint32_t)((double)tv.tv_usec * 4294967296.0 / 1000000.0);
	return (double)*sec + (double)*frac / 4294967296.0;
}

/* Unpredictable 64-bit anti-spoof nonce for the request transmit timestamp:
 * ntp_eval() requires the server to echo these 8 bytes in the originate field, so
 * an off-path attacker who can't predict them can't forge a reply. It is not used
 * for offset math, so it can be fully random; this layers on connect() filtering.
 * Source: getrandom() on Linux, else /dev/urandom (BSD/macOS), else a once-seeded
 * random() XORed with the transmit time - so no platform is left without one. */
static void ntp_nonce(uint32_t *sec, uint32_t *frac)
{
	uint32_t r[2];
	int fd;

#if defined(__linux__) && defined(GRND_NONBLOCK)
	if (getrandom(r, sizeof(r), GRND_NONBLOCK) == (ssize_t)sizeof(r)) {
		*sec = r[0]; *frac = r[1]; return;
	}
#endif
	fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		ssize_t n = read(fd, r, sizeof(r));
		close(fd);
		if (n == (ssize_t)sizeof(r)) { *sec = r[0]; *frac = r[1]; return; }
	}

	/* Fallback: seed once (time + pid so concurrent runs diverge), then XOR the
	 * real transmit time with random() bits. */
	{
		static int seeded = 0;
		uint32_t s, f;

		if (!seeded) {
			struct timeval tv;
			gettimeofday(&tv, NULL);
			srandom((unsigned)(tv.tv_sec ^ tv.tv_usec ^ ((uint32_t)getpid() << 16)));
			seeded = 1;
		}
		(void)ntp_now(&s, &f);
		*sec  = s ^ (uint32_t)random();
		*frac = f ^ (uint32_t)random();
	}
}

/* 48-byte client request (LI=0, VN=4, Mode=3 => 0x23). The transmit timestamp
 * (the anti-spoof nonce) goes in bytes 40..47 - see ntp_nonce() and the origin
 * echo check in ntp_eval(). */
static void ntp_build_request(unsigned char *pkt, uint32_t xmt_sec, uint32_t xmt_frac)
{
	memset(pkt, 0, NTP_PKTSIZE);
	pkt[0] = 0x23;		/* LI=0, VN=4, Mode=3 (client) */
	pkt[2] = 6;		/* poll interval (informational) */
	ntp_put_be32(pkt + 40, xmt_sec);
	ntp_put_be32(pkt + 44, xmt_frac);
}

/* Validate a reply and compute offset/delay (seconds) with the standard NTP
 * formula: offset = ((T2-T1)+(T3-T4))/2, delay = (T4-T1)-(T3-T2). t1/t4 are our
 * transmit/receive times as raw 64-bit NTP stamps (see ntp64_tv); req_xmt is
 * bytes 40..47 of our request, the expected originate echo. Every delta is taken
 * in signed 2^-32 s fixed point (rollover-safe, no float cancellation) and only
 * converted to seconds at the end. Returns NTP_OK or a negative code. */
static int ntp_eval(const unsigned char *r, int len, uint64_t t1, uint64_t t4,
		    const unsigned char *req_xmt,
		    double *offset, double *delay, int *stratum, double *rootdist)
{
	int mode, vn, leap;
	uint64_t t2, t3, reftime;
	int64_t d_t2_t1, d_t3_t4, d_t4_t1, d_t3_t2, off_fp, del_fp;
	double rootdelay, rootdisp;

	if (len < NTP_PKTSIZE) return NTP_ERR_SHORT;

	mode     = r[0] & 0x07;
	vn       = (r[0] >> 3) & 0x07;
	leap     = (r[0] >> 6) & 0x03;
	*stratum = r[1];

	/* A solicited unicast client request is answered with mode 4 (server); a
	 * mode-5 broadcast carries no originate timestamp and could never pass the
	 * echo check, so require mode 4 - tighter and clearer. Authenticate the
	 * originate echo before acting on leap/stratum, so a Kiss-o'-Death (or any
	 * other verdict) is trusted only for a reply that is genuinely ours. A
	 * Kiss-o'-Death also sets leap=3, so stratum 0 must be tested before leap or
	 * the KoD would be misreported as a plain "unsynchronised" and its RATE/DENY
	 * kiss code (surfaced by the caller) lost. */
	if (mode != 4)                       return NTP_ERR_MODE;
	/* RFC 5905 defines versions 1..4; VN 0 or 5..7 is malformed/unsupported. The
	 * origin echo below already authenticates the reply, so this is conformance
	 * rather than anti-spoof, but a reply we cannot interpret is not a usable sample. */
	if (vn < 1 || vn > 4)                return NTP_ERR_VERSION;
	if (memcmp(r + 24, req_xmt, 8) != 0) return NTP_ERR_ORIGIN;
	if (*stratum == 0)                   return NTP_ERR_KOD;		/* refid = kiss code (KoD; also carries leap=3) */
	if (leap == 3)                       return NTP_ERR_LEAP;
	if (*stratum > 15)                   return NTP_ERR_STRATUM;

	t2      = ntp64_wire(r + 32);	/* server receive timestamp   */
	t3      = ntp64_wire(r + 40);	/* server transmit timestamp  */
	reftime = ntp64_wire(r + 16);	/* server reference timestamp */
	/* A synchronised server stamps all three; a zero means it did not. */
	if (t2 == 0)      return NTP_ERR_RXZERO;
	if (t3 == 0)      return NTP_ERR_XMTZERO;
	if (reftime == 0) return NTP_ERR_REFTIME;

	/* Reference timestamp (bytes 16..23): when the server last set its own clock; it
	 * must precede T3 or the server's clock is broken. See NTP_REFTIME_SLOP_FP. */
	if ((int64_t)(reftime - t3) > NTP_REFTIME_SLOP_FP) return NTP_ERR_REFTIME;

	/* A sane server receives (T2) before it transmits (T3); a reversed pair is
	 * broken/hostile, so reject. */
	d_t3_t2 = (int64_t)(t3 - t2);
	if (d_t3_t2 < 0) return NTP_ERR_BADTIME;

	d_t2_t1 = (int64_t)(t2 - t1);
	d_t3_t4 = (int64_t)(t3 - t4);
	d_t4_t1 = (int64_t)(t4 - t1);
	/* Halve each delta before summing rather than (d_t2_t1 + d_t3_t4) / 2:
	 * t2/t3 are server-controlled, so a malicious/garbage server can drive both
	 * deltas near INT64_MAX and overflow their sum (signed overflow is UB). Each
	 * operand here is already <= INT64_MAX/2 so the add can't overflow, at the cost
	 * of one truncated low bit (2^-32 s ~ 0.23 ns - far below timestamp noise). */
	off_fp  = d_t2_t1 / 2 + d_t3_t4 / 2;
	/* del_fp = d_t4_t1 - d_t3_t2 needs the same care: d_t3_t2 is server-controlled
	 * and only floored at 0 above, so it can reach INT64_MAX, while a backward local
	 * clock step between transmit and receive makes d_t4_t1 negative. The plain
	 * subtraction then underflows below INT64_MIN (signed overflow is UB) - reject
	 * instead. A delay that extreme is bogus anyway. (d_t3_t2 >= 0 here, so
	 * INT64_MIN + d_t3_t2 cannot itself overflow.) */
	if (d_t4_t1 < INT64_MIN + d_t3_t2) return NTP_ERR_BADTIME;
	del_fp  = d_t4_t1 - d_t3_t2;
	/* A slightly negative delay is just timestamp granularity on low-latency links
	 * - RFC 5905 clamps it to zero rather than discarding; a materially negative
	 * delay can't be granularity, so reject it as bogus instead of hiding it. */
	if (del_fp < -NTP_DELAY_EPSILON_FP) return NTP_ERR_BADTIME;
	if (del_fp < 0) del_fp = 0;

	*offset = (double)off_fp / NTP_FP_SCALE;
	*delay  = (double)del_fp / NTP_FP_SCALE;

	/* Root distance (RFC 5905): reject a server whose own uncertainty already
	 * exceeds the bound - it answered but is unusable as a time source. Root delay
	 * is a signed 16.16 short (bytes 4..7, slightly negative on fast links); root
	 * dispersion is an unsigned 16.16 short (bytes 8..11). */
	rootdisp  = (double)ntp_be32(r + 8) / 65536.0;
	rootdelay = (double)(int32_t)ntp_be32(r + 4) / 65536.0;
	/* Floor the (signed) root delay at zero before forming the distance: a genuine
	 * fast-link value is only slightly negative (granularity), but a forged large
	 * negative would otherwise cancel an arbitrarily large rootdisp and let an
	 * unusable server pass the bound. Validate the two halves independently. */
	if (rootdelay < 0) rootdelay = 0;
	/* RFC 5905 synchronisation distance includes the locally measured round-trip
	 * delay, not just the server-reported root delay: Lambda = (delta_root +
	 * delta_local)/2 + epsilon_root. Omitting delta_local lets a high-latency or
	 * asymmetric sample (large *delay, tiny rootdelay/rootdisp) slip under the
	 * bound and understates the offset uncertainty reported below. */
	*rootdist = (rootdelay + *delay) / 2.0 + rootdisp;
	if (*rootdist > NTP_MAXROOTDIST) return NTP_ERR_ROOTDIST;
	return NTP_OK;
}

static const char *ntp_strerror(int code)
{
	switch (code) {
	  case NTP_ERR_SHORT:   return "short response packet";
	  case NTP_ERR_MODE:    return "not an NTP server reply";
	  case NTP_ERR_LEAP:    return "server clock not synchronised (leap=3)";
	  case NTP_ERR_STRATUM: return "stratum out of range (>15)";
	  case NTP_ERR_ORIGIN:  return "originate timestamp mismatch (stray/spoofed reply)";
	  case NTP_ERR_XMTZERO: return "server transmit timestamp is zero";
	  case NTP_ERR_RXZERO:  return "server receive timestamp is zero";
	  case NTP_ERR_BADTIME: return "server timestamps inconsistent (transmit before receive, or implausibly negative delay)";
	  case NTP_ERR_ROOTDIST:return "server root distance too large (clock too uncertain to be usable)";
	  case NTP_ERR_REFTIME: return "server reference timestamp missing or in the server's own future (broken clock)";
	  case NTP_ERR_KOD:     return "Kiss-o'-Death (server not usable as a time source)";
	  case NTP_ERR_VERSION: return "reply NTP version out of range (not 1..4)";
	  default:              return "unknown error";
	}
}

/* Probe one host via udp_query_opt() and validate the reply. Returns 0 on a usable
 * synchronised sample (banner carries the offset), 1 if the server is unreachable
 * (no reply within the budget) and 2 if it replied but the sample is unusable; in
 * both error cases the banner carries the reason. offset_out, if non-NULL, gets
 * the offset in seconds for the caller's threshold colouring. T1/T4 are
 * udp_query_opt's precise send/receive times, not the request's transmit timestamp
 * (which is only the echoed nonce). srcip, if non-NULL/non-empty, is the local
 * source address the socket binds to (the host@srcip test setting) for
 * multihomed/ACL-restricted setups. timeout is an optional total-time cap in
 * seconds; <= 0 uses the probe's own retry policy (NTP_PROBE_TRIES x NTP_PROBE_TRY_SEC). */
int ntp_internal_probe(const char *ip, const char *srcip, int timeout, strbuffer_t *banner, double *offset_out)
{
	unsigned char req[NTP_PKTSIZE], resp[NTP_PKTSIZE + 64];
	uint32_t xmt_sec, xmt_frac;
	uint64_t t1, t4;
	double offset = 0.0, delay = 0.0, rootdist = 0.0;
	int n = -1, eval, stratum = 0, attempt, tries, per_try;
	struct timeval sent, recv;
	udp_query_opts qopts;
	const char *err = "no response from server";
	char line[256];

	memset(&qopts, 0, sizeof(qopts));
	qopts.srcip = srcip;	/* NULL/empty => no bind, identical to the defaults */

	clearstrbuffer(banner);

	/* Retransmission policy is the probe's own: NTP_PROBE_TRIES attempts of
	 * NTP_PROBE_TRY_SEC each. A positive timeout is an optional *tighter* total-time
	 * cap (a caller with its own deadline); timeout <= 0 means "use the full policy".
	 * Shrinking only kicks in when the cap is below the default budget, so a small
	 * cap still does one short try and never overruns. */
	per_try = NTP_PROBE_TRY_SEC;
	tries   = NTP_PROBE_TRIES;
	if (timeout > 0 && timeout < tries * per_try) {
		if (per_try > timeout) per_try = timeout;	/* per_try >= 1 (timeout >= 1 here) */
		tries = timeout / per_try;			/* >= 1 */
	}

	/* Retransmit lost datagrams. Each attempt is independent - a fresh nonce,
	 * request and (inside udp_query_opt) a freshly connected socket on a new
	 * ephemeral port - so a delayed reply to an earlier attempt lands on a now
	 * closed port and can't be mistaken for this one. We retry only transport
	 * failure (no reply within per_try); a reply that fails ntp_eval validation
	 * is not retried, as those failures (unsynced, KoD, mode) are persistent.
	 *
	 * The NTP service is hardcoded: the ntp test has no per-host port override,
	 * so keeping the signature ip-only loses nothing. We pass the "ntp" service
	 * name (not "123"); getaddrinfo() resolves it via /etc/services. */
	for (attempt = 0; attempt < tries; attempt++) {
		ntp_nonce(&xmt_sec, &xmt_frac);		/* unpredictable request nonce (anti-spoof) */
		ntp_build_request(req, xmt_sec, xmt_frac);
		n = udp_query_opt(ip, "ntp", req, sizeof(req), resp, sizeof(resp), per_try,
				  &qopts, &sent, &recv, &err);
		if (n >= 0) break;
	}
	if (n < 0) {
		/* No reply within the budget: down or filtered. Kept distinct (return 1,
		 * "unreachable") from a reply that fails validation (return 2, "replied
		 * but unusable") so an operator can tell the two apart. */
		snprintf(line, sizeof(line), "NTP server %s is unreachable: %s\n", ip, err);
		addtobuffer(banner, line);
		return 1;
	}

	t1 = ntp64_tv(&sent);
	t4 = ntp64_tv(&recv);
	eval = ntp_eval(resp, n, t1, t4, req + 40, &offset, &delay, &stratum, &rootdist);
	if (eval != NTP_OK) {
		if (eval == NTP_ERR_KOD) {
			/* Kiss-o'-Death: the 4-byte reference id (bytes 12..15) is an ASCII
			 * kiss code - RATE (rate-limited), DENY/RSTR (access denied). Surface
			 * it so an operator sees a server-side policy block, not a generic
			 * failure; sanitise non-printable bytes from a hostile packet. */
			char kiss[5];
			int i;

			memcpy(kiss, resp + 12, 4);
			kiss[4] = '\0';
			for (i = 0; i < 4; i++) if (!isprint((unsigned char)kiss[i])) kiss[i] = '?';
			snprintf(line, sizeof(line),
				 "NTP server %s sent a Kiss-o'-Death (%s) and is not usable as a time source\n",
				 ip, kiss);
		}
		else {
			snprintf(line, sizeof(line), "NTP server %s replied but is unusable: %s\n",
				 ip, ntp_strerror(eval));
		}
		addtobuffer(banner, line);
		return 2;	/* responded, but not a usable time sample */
	}

	/* Status banner. do_net.c reads the clock offset back from "offset <sec>"
	 * (seconds) and records it in ntp.rrd (scaled to ms). The stratum, the
	 * "+/-" bound (RFC 5905 root distance = (rootdelay+delay)/2 + rootdisp,
	 * validated against NTP_MAXROOTDIST) and delay are shown for the operator. */
	snprintf(line, sizeof(line),
		 "NTP server %s is synchronised\n\n"
		 "stratum %d, offset %+.6f +/- %.6f sec, delay %.6f sec\n",
		 ip, stratum, offset, rootdist, delay);
	addtobuffer(banner, line);
	if (offset_out) *offset_out = offset;
	return 0;
}
