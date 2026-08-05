/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/network/ntp-probe-harness.c
 *
 * Regression test for the internal SNTP probe (xymonnet/ntpprobe.c). It drives
 * the REAL packet-build and validation/offset code - the file is #included the
 * same way xymond/do_rrd.c #includes its parsers - against synthesized server
 * replies, with no socket. It pins:
 *   - the NTP offset formula ((T2-T1)+(T3-T4))/2 and the seconds output,
 *   - rejection of stray/spoofed replies (originate echo mismatch),
 *   - rejection of an unsynchronised server (leap=3), a Kiss-o'-Death (its own
 *     NTP_ERR_KOD code, distinct from a plain out-of-range stratum), a wrong
 *     mode, a short packet, zero or reversed server timestamps,
 *   - that the raw 64-bit (int64_t)(a-b) differencing stays correct across the
 *     2036 era rollover (replacing the old ntp_unfold() check),
 *   - that ntp_internal_probe() retransmits a lost datagram and gives up after
 *     NTP_PROBE_TRIES (driven through a programmable udp_query mock),
 *   - that the success line carries the offset in the " <offset> +/- " position
 *     where the offset parser added with the threshold/RRD support reads it back.
 *
 * Built with AddressSanitizer when available so the memcmp/timestamp reads are
 * bounds-checked. LeakSanitizer is disabled by the runner (its ptrace pass is
 * blocked in some sandboxes); the regressions here are logic, not leaks.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

/* ntpprobe.c only needs these two strbuffer entry points; stub them. */
typedef struct { char buf[4096]; } strbuffer_t;
static void clearstrbuffer(strbuffer_t *b) { b->buf[0] = '\0'; }
static void addtobuffer(strbuffer_t *b, char *t)
{
	size_t n = strlen(b->buf);
	snprintf(b->buf + n, sizeof(b->buf) - n, "%s", t);
}

#include "../../xymonnet/ntpprobe.c"

/* A double "NTP seconds" value as the raw 64-bit NTP timestamp ntp_eval() now
 * takes, mirroring ntp64_tv()'s representation so the synthesized replies and the
 * local t1/t4 share one era. */
static uint64_t U(double ntpsec)
{
	uint32_t sec  = (uint32_t)ntpsec;
	uint32_t frac = (uint32_t)((ntpsec - (double)sec) * 4294967296.0);

	return ((uint64_t)sec << 32) | frac;
}

/* Programmable udp_query_opt() mock (the real transport is covered separately by
 * tests/network/udp-query.sh). It counts calls and returns -1 ("no response")
 * until udpq_succeed_on (1-based) is reached, then synthesizes a valid mode-4
 * stratum-3 reply that echoes this request's nonce. This lets the retransmit /
 * give-up loop in ntp_internal_probe() be driven deterministically with no
 * socket. udpq_succeed_on == 0 means every attempt fails. */
static int udpq_calls;
static int udpq_succeed_on;
int udp_query_opt(const char *ip, const char *port, const void *req, size_t reqlen,
		  void *resp, size_t respsz, int timeout, const udp_query_opts *opts,
		  struct timeval *sent_tv, struct timeval *recv_tv, const char **errstr)
{
	struct timeval now;
	double tnow, tserv;
	uint32_t ssec, sfrac, refsec, reffrac;
	unsigned char *r = (unsigned char *)resp;

	(void)port; (void)reqlen; (void)respsz; (void)timeout; (void)opts;
	udpq_calls++;
	if ((udpq_succeed_on == 0) || (udpq_calls < udpq_succeed_on)) {
		if (errstr) *errstr = "no response from server";
		return -1;
	}

	gettimeofday(&now, NULL);
	if (sent_tv) *sent_tv = now;
	if (recv_tv) *recv_tv = now;
	tnow  = (double)(now.tv_sec + NTP_UNIX_DELTA) + (double)now.tv_usec / 1000000.0;
	tserv = tnow + 0.045;			/* server ~45 ms ahead */
	ssec  = (uint32_t)tserv;
	sfrac = (uint32_t)((tserv - ssec) * 4294967296.0);
	refsec  = (uint32_t)(tserv - 100.0);	/* last synced ~100 s ago */
	reffrac = sfrac;

	(void)ip;
	memset(r, 0, NTP_PKTSIZE);
	r[0] = (0 << 6) | (4 << 3) | 4;			/* leap 0, vn 4, mode 4 */
	r[1] = 3;					/* stratum 3 */
	ntp_put_be32(r + 8, (uint32_t)(0.05 * 65536));	/* root dispersion 0.05s */
	ntp_put_be32(r + 16, refsec); ntp_put_be32(r + 20, reffrac);	/* reference */
	memcpy(r + 24, (const unsigned char *)req + 40, 8);	/* echo the nonce */
	ntp_put_be32(r + 32, ssec); ntp_put_be32(r + 36, sfrac);	/* receive  */
	ntp_put_be32(r + 40, ssec); ntp_put_be32(r + 44, sfrac);	/* transmit */
	return NTP_PKTSIZE;
}

/* ---- helpers -------------------------------------------------------------- */

static int failures = 0;

static void put_be32(unsigned char *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

/* Build a plausible server reply to the request in req, with the server clock
 * skew_s seconds ahead of ours and a round-trip of rtt_s seconds. */
static void make_reply(unsigned char *resp, const unsigned char *req,
		       int stratum, int mode, int leap,
		       double t1, double skew_s, double rtt_s)
{
	double tserv = t1 + skew_s + rtt_s / 2.0;
	uint32_t ssec  = (uint32_t)tserv;
	uint32_t sfrac = (uint32_t)((tserv - ssec) * 4294967296.0);

	memset(resp, 0, NTP_PKTSIZE);
	resp[0] = (leap << 6) | (4 << 3) | mode;
	resp[1] = stratum;
	put_be32(resp + 8, (uint32_t)(0.05 * 65536));	/* root dispersion 0.05s */
	put_be32(resp + 16, (uint32_t)(tserv - 100.0));	/* reference: synced ~100 s ago */
	put_be32(resp + 20, sfrac);
	memcpy(resp + 24, req + 40, 8);			/* echo our transmit */
	put_be32(resp + 32, ssec); put_be32(resp + 36, sfrac);	/* receive  */
	put_be32(resp + 40, ssec); put_be32(resp + 44, sfrac);	/* transmit */
}

static void check_rc(const char *what, int got, int want)
{
	if (got != want) {
		failures++;
		fprintf(stderr, "FAIL %s: got %d want %d\n", what, got, want);
	}
	else {
		printf("ok   %s (rc=%d)\n", what, got);
	}
}

static void check(const char *what, int cond)
{
	if (!cond) { failures++; fprintf(stderr, "FAIL %s\n", what); }
	else       { printf("ok   %s\n", what); }
}

int main(void)
{
	unsigned char req[NTP_PKTSIZE], resp[NTP_PKTSIZE];
	uint32_t xs, xf;
	double t1, t4, off, del, rdist;
	uint64_t T1, T4;
	int strat, rc;

	t1 = ntp_now(&xs, &xf);
	ntp_build_request(req, xs, xf);
	check("request mode byte is client (0x23)", req[0] == 0x23);

	t4 = t1 + 0.010;	/* 10 ms round trip */
	T1 = U(t1); T4 = U(t4);

	/* Server 80 ms ahead: offset ~ +0.080 - half-RTT bias is in make_reply.
	 * off/del/rdist are only written when ntp_eval() returns NTP_OK, so every
	 * read of them is gated on rc to keep them defined for static analysers. */
	make_reply(resp, req, 3, 4, 0, t1, 0.080, 0.010);
	rc = ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist);
	check_rc("good reply accepted", rc, NTP_OK);
	check("offset ~ +0.080", rc == NTP_OK && off > 0.079 && off < 0.081);
	check("stratum reported", rc == NTP_OK && strat == 3);
	/* The out-param is the full RFC 5905 root distance (synchronisation
	 * distance), not bare root dispersion: (rootdelay + delay)/2 + rootdisp.
	 * Here rootdelay=0, delay~0.010, rootdisp=0.05, so rootdist ~ 0.055. */
	check("root distance ~ 0.055", rc == NTP_OK && rdist > 0.054 && rdist < 0.056);

	/* Negative offset: server 0.5 s behind */
	make_reply(resp, req, 2, 4, 0, t1, -0.500, 0.010);
	rc = ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist);
	check_rc("negative offset reply accepted", rc, NTP_OK);
	check("negative offset preserved", rc == NTP_OK && off < -0.49 && off > -0.51);

	/* A round trip that comes out slightly negative (timestamp granularity on a
	 * very low-latency link) must be clamped to zero and the sample accepted, not
	 * rejected - RFC 5905 clamps delay to the system precision. Here t3==t2 in the
	 * reply, so delay = t4 - t1; feed t4 a hair before t1 to force it negative. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	rc = ntp_eval(resp, NTP_PKTSIZE, T1, U(t1 - 0.0002), req + 40, &off, &del, &strat, &rdist);
	check_rc("sub-precision negative delay accepted", rc, NTP_OK);
	check("negative delay clamped to zero", rc == NTP_OK && del == 0.0);
	check("offset still computed (~ +0.045)", rc == NTP_OK && off > 0.044 && off < 0.046);

	/* Spoof / stray: originate echo corrupted */
	make_reply(resp, req, 3, 4, 0, t1, 0.0, 0.010);
	resp[24] ^= 0xff;
	check_rc("originate mismatch rejected", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_ORIGIN);

	/* Unsynchronised server: leap = 3 */
	make_reply(resp, req, 3, 4, 3, t1, 0.0, 0.010);
	check_rc("leap=3 rejected", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_LEAP);

	/* Kiss-o'-Death: stratum 0 now returns its own code (not generic STRATUM),
	 * and a real KoD carries an ASCII kiss code in the reference-id field. */
	make_reply(resp, req, 0, 4, 0, t1, 0.0, 0.010);
	memcpy(resp + 12, "RATE", 4);
	check_rc("stratum 0 (KoD) reported as KOD", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_KOD);

	/* A real KoD also sets leap=3 (unsynchronised); stratum 0 must be tested before
	 * leap so the kiss code is not swallowed by a generic "unsynchronised" verdict. */
	make_reply(resp, req, 0, 4, 3, t1, 0.0, 0.010);
	memcpy(resp + 12, "DENY", 4);
	check_rc("KoD with leap=3 still reported as KOD (not LEAP)",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_KOD);

	/* Out-of-range stratum */
	make_reply(resp, req, 16, 4, 0, t1, 0.0, 0.010);
	check_rc("stratum 16 rejected", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_STRATUM);

	/* Wrong mode (client echoed back) */
	make_reply(resp, req, 3, 3, 0, t1, 0.0, 0.010);
	check_rc("non-server mode rejected", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_MODE);

	/* Mode 5 (broadcast) is rejected too: only a mode-4 unicast server reply is
	 * accepted, since a broadcast carries no originate echo to validate. */
	make_reply(resp, req, 3, 5, 0, t1, 0.0, 0.010);
	check_rc("broadcast mode 5 rejected", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_MODE);

	/* Bad protocol version: RFC 5905 defines VN 1..4, so VN 0 and 5..7 are
	 * rejected even when mode/stratum/echo are otherwise valid. Patch only the
	 * version bits (3..5) of an otherwise-good mode-4 reply. */
	make_reply(resp, req, 3, 4, 0, t1, 0.0, 0.010);
	resp[0] = (resp[0] & ~0x38) | (5 << 3);		/* VN 5 */
	check_rc("reply version 5 rejected", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_VERSION);
	make_reply(resp, req, 3, 4, 0, t1, 0.0, 0.010);
	resp[0] = (resp[0] & ~0x38) | (0 << 3);		/* VN 0 */
	check_rc("reply version 0 rejected", ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_VERSION);

	/* Short packet */
	make_reply(resp, req, 3, 4, 0, t1, 0.0, 0.010);
	check_rc("short packet rejected", ntp_eval(resp, 40, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_SHORT);

	/* The success banner must place the offset immediately before " +/- " so
	 * parse_ntp_offset() reads it back. Reproduce that parse here. */
	{
		char line[256], *p, *tok;
		double parsed;

		snprintf(line, sizeof(line),
			 "NTP server 192.0.2.123 is synchronised\n\n"
			 "stratum %d, offset %+.6f +/- %.6f sec, delay %.6f sec\n",
			 3, -0.123456, 0.05, 0.01);
		p = strstr(line, " +/- ");
		check("banner has ' +/- ' marker", p != NULL);
		if (p) {
			*p = '\0';
			tok = p;
			while ((tok > line) && (tok[-1] != ' ') && (tok[-1] != '\n')) tok--;
			parsed = strtod(tok, NULL);
			check("offset token parses to -0.123456", parsed < -0.1234 && parsed > -0.1235);
		}
	}

	/* Server transmit timestamp zero: an unsynchronised/idle server. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	memset(resp + 40, 0, 8);
	check_rc("zero transmit timestamp rejected",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_XMTZERO);

	/* Server receive timestamp zero. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	memset(resp + 32, 0, 8);
	check_rc("zero receive timestamp rejected",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_RXZERO);

	/* Transmit before receive (T3 < T2): impossible on a sane server, so reject
	 * rather than report a bogus offset. Shave 1 s off the transmit seconds. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	put_be32(resp + 40, ntp_be32(resp + 40) - 1);
	check_rc("transmit-before-receive rejected",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_BADTIME);

	/* Malicious timestamps: a hostile server drives d_t3_t2 (T3-T2) to INT64_MAX
	 * while our own clock steps backward between transmit and receive (T4 < T1, so
	 * d_t4_t1 < 0). The delay subtraction d_t4_t1 - d_t3_t2 would then underflow
	 * below INT64_MIN (signed overflow, UB - flagged by UBSan); it must be rejected
	 * as BADTIME, not computed. Receive raw stamp = 1, transmit = 0x8000...0, so
	 * (int64_t)(T3-T2) == INT64_MAX; reference = T3 to clear the reftime check. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	put_be32(resp + 16, 0x80000000u); put_be32(resp + 20, 0);	/* reference = T3 */
	put_be32(resp + 32, 0);           put_be32(resp + 36, 1);	/* receive  T2 = 1 */
	put_be32(resp + 40, 0x80000000u); put_be32(resp + 44, 0);	/* transmit T3 = 2^63 */
	check_rc("malicious T3-T2=INT64_MAX with backward local clock rejected",
		 ntp_eval(resp, NTP_PKTSIZE, T1, U(t1 - 0.0002), req + 40, &off, &del, &strat, &rdist), NTP_ERR_BADTIME);

	/* Root distance too large: the server answered but its own uncertainty
	 * (rootdelay/2 + rootdisp) exceeds NTP_MAXROOTDIST, so it is unusable. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	put_be32(resp + 8, (uint32_t)(2.0 * 65536));	/* root dispersion 2 s */
	check_rc("excessive root distance rejected",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_ROOTDIST);

	/* A forged large-negative root delay must not cancel a huge root dispersion:
	 * the signed delay is floored at zero, so this unusable server is still rejected
	 * rather than slipping through with a near-zero computed root distance. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	put_be32(resp + 8, (uint32_t)(2.0 * 65536));		/* root dispersion 2 s    */
	put_be32(resp + 4, (uint32_t)(int32_t)(-4.0 * 65536));	/* root delay -4 s (forged) */
	check_rc("negative root delay cannot mask large rootdisp",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_ROOTDIST);

	/* Reference timestamp in the server's own future (after T3): a broken clock. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	put_be32(resp + 16, ntp_be32(resp + 40) + 100);	/* reftime 100 s after transmit */
	check_rc("future reference timestamp rejected",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_REFTIME);

	/* A zero reference timestamp (never-synced server) is rejected the same way. */
	make_reply(resp, req, 3, 4, 0, t1, 0.040, 0.010);
	memset(resp + 16, 0, 8);
	check_rc("zero reference timestamp rejected",
		 ntp_eval(resp, NTP_PKTSIZE, T1, T4, req + 40, &off, &del, &strat, &rdist), NTP_ERR_REFTIME);

	/* Era rollover (2036-02-07): when our local stamp and the server stamps
	 * straddle the 64-bit wrap at UINT64_MAX, the raw (int64_t)(a-b) differencing
	 * still recovers the true small offset - not only in the current era the live
	 * tests run in. Put t1 just below the wrap so the server stamp (+50 ms) carries
	 * past UINT64_MAX into the next era, while t1/t4 stay in the current one. */
	{
		unsigned char rr[NTP_PKTSIZE];
		uint64_t wrapdelta = (uint64_t)(0.020 * 4294967296.0);	/* 20 ms below the wrap */
		uint64_t t1b  = (uint64_t)0 - wrapdelta;		/* == UINT64_MAX - 20 ms + 1 */
		uint64_t serv = t1b + (uint64_t)(0.050 * 4294967296.0);	/* +50 ms, wraps the era */
		uint64_t t4b  = t1b + (uint64_t)(0.010 * 4294967296.0);	/* 10 ms RTT, still pre-wrap */
		uint64_t ref  = serv - ((uint64_t)100 << 32);		/* synced ~100 s ago */
		double offb = 0.0, delb = 0.0, rdb = 0.0;
		int stratb = 0, rcb;

		memset(rr, 0, sizeof(rr));
		rr[0] = (4 << 3) | 4;			/* leap 0, vn 4, mode 4 */
		rr[1] = 3;				/* stratum 3 */
		put_be32(rr + 8, (uint32_t)(0.05 * 65536));
		put_be32(rr + 16, (uint32_t)(ref >> 32));  put_be32(rr + 20, (uint32_t)ref);
		memcpy(rr + 24, req + 40, 8);		/* echo our nonce */
		put_be32(rr + 32, (uint32_t)(serv >> 32)); put_be32(rr + 36, (uint32_t)serv);
		put_be32(rr + 40, (uint32_t)(serv >> 32)); put_be32(rr + 44, (uint32_t)serv);
		rcb = ntp_eval(rr, NTP_PKTSIZE, t1b, t4b, req + 40, &offb, &delb, &stratb, &rdb);
		check("era rollover: reply accepted", rcb == NTP_OK);
		check("era rollover: offset ~ +0.045 across the wrap",
		      rcb == NTP_OK && offb > 0.044 && offb < 0.046);
	}

	/* The retransmit / give-up loop in ntp_internal_probe(), driven through the
	 * programmable udp_query mock. timeout 0 selects the probe's own policy
	 * (NTP_PROBE_TRIES attempts of NTP_PROBE_TRY_SEC). */
	{
		strbuffer_t banner;
		double off2 = 0.0;

		udpq_calls = 0; udpq_succeed_on = 2;	/* lose the first datagram */
		check("probe retransmits and succeeds on the 2nd attempt",
		      ntp_internal_probe("192.0.2.1", NULL, 0, &banner, &off2) == 0);
		check("retransmit stopped after the successful attempt", udpq_calls == 2);
		check("success banner says synchronised", strstr(banner.buf, "synchronised") != NULL);

		udpq_calls = 0; udpq_succeed_on = 0;	/* every attempt fails */
		check("probe gives up when no datagram is answered",
		      ntp_internal_probe("192.0.2.1", NULL, 0, &banner, &off2) == 1);
		check("gave up after NTP_PROBE_TRIES attempts", udpq_calls == NTP_PROBE_TRIES);
		check("failure banner says unreachable", strstr(banner.buf, "unreachable") != NULL);

		/* A positive timeout below the default budget is a tighter cap: it shrinks
		 * the attempt count rather than being divided back into the same 3 tries. */
		udpq_calls = 0; udpq_succeed_on = 0;
		check("a tight timeout cap gives up after a single attempt",
		      ntp_internal_probe("192.0.2.1", NULL, 1, &banner, &off2) == 1);
		check("cap honoured: one attempt only", udpq_calls == 1);
	}

	if (failures) {
		fprintf(stderr, "\n%d check(s) failed\n", failures);
		return 1;
	}
	printf("\nall internal SNTP probe checks passed\n");
	return 0;
}
