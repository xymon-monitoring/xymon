/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/network/udp-query-harness.c
 *
 * Drives the reusable UDP transport (xymonnet/udpquery.c) over the loopback
 * interface and pins its contract and hardening:
 *   - request/echo round-trip; send/receive timestamps set and ordered,
 *   - closed-port error path and a genuine timeout against a silent listener,
 *   - the NULL-timestamp (non-NTP) path returns bytes, not clock-step gated,
 *   - sub-second timeout via udp_query_opt(timeout_us),
 *   - truncation: UDPQ_DETECT_TRUNC fails, the default truncates silently,
 *   - source filtering: a reply from a wrong port is dropped (connect()),
 *   - EINTR: a signal mid-wait is retried, the delayed reply still arrives,
 *   - source binding: opts.srcip round-trips, a wrong-family source fails clean.
 * (The wall-clock-step rejection needs settimeofday(), so it is guarded by
 * inspection, not here - see the NOTE near the end.) Exits 77 (skip) if the
 * sandbox forbids the loopback socket setup, so a locked-down CI box does not
 * fail the suite.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../../xymonnet/udpquery.c"

#define SKIP 77

static int failures = 0;

/* For the EINTR case: a no-op handler installed WITHOUT SA_RESTART, so a signal
 * delivered during the wait makes select()/recv() return EINTR. */
static volatile sig_atomic_t eintr_caught = 0;
static void on_sigusr1(int s) { (void)s; eintr_caught = 1; }
static void check(const char *what, int cond)
{
	if (!cond) { failures++; fprintf(stderr, "FAIL %s\n", what); }
	else       { printf("ok   %s\n", what); }
}

/* Bind a UDP socket on 127.0.0.1:0, return fd and the chosen port (host order). */
static int bind_loopback(int *port)
{
	struct sockaddr_in sa;
	socklen_t sl = sizeof(sa);
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0) return -1;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = 0;
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
	if (getsockname(fd, (struct sockaddr *)&sa, &sl) != 0)  { close(fd); return -1; }
	*port = ntohs(sa.sin_port);
	return fd;
}

/* Fork a one-shot server that, on the first datagram, replies with replybytes
 * bytes (so the client can exercise truncation). Returns the child pid and the
 * bound port, or -1. */
static pid_t fork_reply_server(int replybytes, int *port)
{
	int s = bind_loopback(port);
	pid_t k;

	if (s < 0) return -1;
	k = fork();
	if (k < 0) { close(s); return -1; }
	if (k == 0) {
		struct sockaddr_in cli; socklen_t cl = sizeof(cli);
		char buf[64], reply[256];
		int m = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cl);
		if (m >= 0) {
			if (replybytes > (int)sizeof(reply)) replybytes = sizeof(reply);
			memset(reply, 'R', replybytes);
			sendto(s, reply, replybytes, 0, (struct sockaddr *)&cli, cl);
		}
		close(s); _exit(0);
	}
	close(s);
	return k;
}

int main(void)
{
	int srv, port, n;
	char portstr[16], resp[64];
	const char *err = NULL;
	struct timeval sent, recv;
	pid_t kid;

	alarm(30);	/* never let a wedged socket/child hang CI: SIGALRM -> non-zero exit */

	srv = bind_loopback(&port);
	if (srv < 0) { fprintf(stderr, "SKIP: cannot bind loopback UDP socket\n"); return SKIP; }
	snprintf(portstr, sizeof(portstr), "%d", port);

	/* Child: echo one datagram back to its sender, then exit. */
	kid = fork();
	if (kid < 0) { fprintf(stderr, "SKIP: fork failed\n"); close(srv); return SKIP; }
	if (kid == 0) {
		struct sockaddr_in cli;
		socklen_t cl = sizeof(cli);
		char buf[64];
		int m = recvfrom(srv, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cl);
		if (m > 0) sendto(srv, buf, m, 0, (struct sockaddr *)&cli, cl);
		close(srv);
		_exit(0);
	}
	close(srv);	/* parent does not use the server fd */

	/* 1. Round-trip: udp_query sends "PING", the echo comes back. */
	memset(&sent, 0, sizeof(sent));
	memset(&recv, 0, sizeof(recv));
	n = udp_query_opt("127.0.0.1", portstr, "PING", 4, resp, sizeof(resp), 3, NULL, &sent, &recv, &err);
	check("round-trip returns 4 bytes", n == 4);
	check("echoed payload matches", (n == 4) && (memcmp(resp, "PING", 4) == 0));
	check("send timestamp set", sent.tv_sec != 0);
	check("recv timestamp set", recv.tv_sec != 0);
	check("recv not before send",
	      (recv.tv_sec > sent.tv_sec) ||
	      ((recv.tv_sec == sent.tv_sec) && (recv.tv_usec >= sent.tv_usec)));

	{ int st; waitpid(kid, &st, 0); }

	/* 2. Timeout: nothing is listening on this freshly-closed port. */
	{
		int p2 = 0; int tmp = bind_loopback(&p2); char p2s[16];
		if (tmp < 0) { fprintf(stderr, "SKIP: cannot bind loopback UDP socket\n"); return SKIP; }
		close(tmp);			/* free the port so the query goes unanswered */
		snprintf(p2s, sizeof(p2s), "%d", p2);
		err = NULL;
		n = udp_query_opt("127.0.0.1", p2s, "X", 1, resp, sizeof(resp), 1, NULL, NULL, NULL, &err);
		check("closed port returns -1", n == -1);
		check("timeout reason reported", err != NULL);
	}

	/* 3. Genuine select() timeout: a socket that receives the datagram but never
	 *    replies. Unlike the closed port above (ICMP unreachable -> fast error),
	 *    here udp_query_opt() must wait out the timeout before reporting failure. */
	{
		int silent, ps; char pss[16];
		silent = bind_loopback(&ps);	/* bound, kept open, never echoes */
		if (silent < 0) { fprintf(stderr, "SKIP: cannot bind silent socket\n"); return SKIP; }
		snprintf(pss, sizeof(pss), "%d", ps);
		err = NULL;
		n = udp_query_opt("127.0.0.1", pss, "X", 1, resp, sizeof(resp), 1, NULL, NULL, NULL, &err);
		check("unanswered query times out (-1)", n == -1);
		check("timeout reason reported", err != NULL);
		close(silent);
	}

	/* 4. A non-timing probe (DNS/SNMP/...) passes NULL timestamps and wants only
	 *    the response bytes; the clock-step rejection must not apply. We can't
	 *    force a clock step here, but pin that the NULL-timestamp path round-trips
	 *    a reply, which is the general-purpose contract. */
	{
		int s2, p2; char p2s[16]; pid_t k2;

		s2 = bind_loopback(&p2);
		if (s2 < 0) { fprintf(stderr, "SKIP: cannot bind loopback UDP socket\n"); return SKIP; }
		snprintf(p2s, sizeof(p2s), "%d", p2);
		k2 = fork();
		if (k2 < 0) { fprintf(stderr, "SKIP: fork failed\n"); close(s2); return SKIP; }
		if (k2 == 0) {
			struct sockaddr_in cli; socklen_t cl = sizeof(cli); char buf[64];
			int m = recvfrom(s2, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cl);
			if (m > 0) sendto(s2, buf, m, 0, (struct sockaddr *)&cli, cl);
			close(s2); _exit(0);
		}
		close(s2);
		err = NULL;
		n = udp_query_opt("127.0.0.1", p2s, "PONG", 4, resp, sizeof(resp), 3, NULL, NULL, NULL, &err);
		check("NULL-timestamp round-trip returns 4 bytes", n == 4);
		check("NULL-timestamp payload matches", (n == 4) && (memcmp(resp, "PONG", 4) == 0));
		{ int st; waitpid(k2, &st, 0); }
	}

	/* 5. Sub-second timeout via udp_query_opt: a 200 ms budget against a silent
	 *    listener must fail well under the old 1 s floor (the 99 s arg is
	 *    overridden by timeout_us). */
	{
		int silent, ps; char pss[16];
		struct timeval a, b; long elapsed_ms;
		udp_query_opts o;

		memset(&o, 0, sizeof(o)); o.timeout_us = 200000;
		silent = bind_loopback(&ps);
		if (silent < 0) { fprintf(stderr, "SKIP: cannot bind loopback UDP socket\n"); return SKIP; }
		snprintf(pss, sizeof(pss), "%d", ps);
		err = NULL;
		gettimeofday(&a, NULL);
		n = udp_query_opt("127.0.0.1", pss, "X", 1, resp, sizeof(resp), 99, &o, NULL, NULL, &err);
		gettimeofday(&b, NULL);
		elapsed_ms = (b.tv_sec - a.tv_sec) * 1000L + (b.tv_usec - a.tv_usec) / 1000L;
		check("sub-second timeout returns -1", n == -1);
		check("sub-second timeout honoured (well under 1s)", elapsed_ms < 800);
		close(silent);
	}

	/* 6. Truncation: a reply larger than respsz. With UDPQ_DETECT_TRUNC it is a
	 *    failure; without the flag the truncated head is returned (the default). */
	{
		int p6; char p6s[16]; pid_t k6; int st;
		udp_query_opts o;

		memset(&o, 0, sizeof(o)); o.flags = UDPQ_DETECT_TRUNC;
		k6 = fork_reply_server(100, &p6);
		if (k6 < 0) { fprintf(stderr, "SKIP: cannot fork reply server\n"); return SKIP; }
		snprintf(p6s, sizeof(p6s), "%d", p6);
		err = NULL;
		n = udp_query_opt("127.0.0.1", p6s, "X", 1, resp, 8, 3, &o, NULL, NULL, &err);
		check("oversized reply rejected with UDPQ_DETECT_TRUNC", n == -1);
		check("truncation reason reported", err != NULL);
		waitpid(k6, &st, 0);

		k6 = fork_reply_server(100, &p6);
		if (k6 < 0) { fprintf(stderr, "SKIP: cannot fork reply server\n"); return SKIP; }
		snprintf(p6s, sizeof(p6s), "%d", p6);
		n = udp_query_opt("127.0.0.1", p6s, "X", 1, resp, 8, 3, NULL, NULL, NULL, &err);
		check("oversized reply truncated silently by default", n == 8);
		waitpid(k6, &st, 0);
	}

	/* 7. Source filtering: a reply from a different port than queried must be
	 *    dropped by the connect()ed socket, so the query times out rather than
	 *    accepting the stray/spoofed datagram. Locks in the connect() hardening
	 *    that a conn_* DGRAM port must keep (strict peer by default). */
	{
		int sa, pa, sb, pb; char pas[16]; pid_t k; int st;

		sa = bind_loopback(&pa);
		sb = bind_loopback(&pb);
		if ((sa < 0) || (sb < 0)) { fprintf(stderr, "SKIP: cannot bind loopback sockets\n"); return SKIP; }
		snprintf(pas, sizeof(pas), "%d", pa);
		k = fork();
		if (k < 0) { fprintf(stderr, "SKIP: fork failed\n"); close(sa); close(sb); return SKIP; }
		if (k == 0) {
			/* Receive the query on the real port, answer from the OTHER port. */
			struct sockaddr_in cli; socklen_t cl = sizeof(cli); char buf[64];
			int m = recvfrom(sa, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cl);
			if (m >= 0) sendto(sb, "SPOOF", 5, 0, (struct sockaddr *)&cli, cl);
			close(sa); close(sb); _exit(0);
		}
		close(sa); close(sb);
		err = NULL;
		n = udp_query_opt("127.0.0.1", pas, "X", 1, resp, sizeof(resp), 1, NULL, NULL, NULL, &err);
		check("reply from a wrong source port is dropped (query times out)", n == -1);
		waitpid(k, &st, 0);
	}

	/* 8. EINTR: a signal delivered while udp_query is waiting must not abort or
	 *    shorten the query - the wait is retried and the delayed reply still
	 *    arrives. A server replies after 400 ms while a sibling fires SIGUSR1 at
	 *    ~100/200 ms; the query must still return the reply. */
	{
		int s8, p8; char p8s[16]; pid_t srvk, sigk; int st;
		struct sigaction act, old;

		memset(&act, 0, sizeof(act)); act.sa_handler = on_sigusr1;	/* no SA_RESTART */
		sigaction(SIGUSR1, &act, &old);
		eintr_caught = 0;

		s8 = bind_loopback(&p8);
		if (s8 < 0) { fprintf(stderr, "SKIP: cannot bind loopback socket\n"); sigaction(SIGUSR1, &old, NULL); return SKIP; }
		snprintf(p8s, sizeof(p8s), "%d", p8);

		srvk = fork();
		if (srvk < 0) { fprintf(stderr, "SKIP: fork failed\n"); close(s8); sigaction(SIGUSR1, &old, NULL); return SKIP; }
		if (srvk == 0) {
			struct sockaddr_in cli; socklen_t cl = sizeof(cli); char buf[64];
			int m = recvfrom(s8, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cl);
			usleep(400000);					/* reply after 400 ms */
			if (m > 0) sendto(s8, buf, m, 0, (struct sockaddr *)&cli, cl);
			close(s8); _exit(0);
		}
		close(s8);

		sigk = fork();
		if (sigk == 0) {
			pid_t pp = getppid();
			usleep(100000); kill(pp, SIGUSR1);		/* ~100 ms into the wait */
			usleep(100000); kill(pp, SIGUSR1);		/* ~200 ms into the wait */
			_exit(0);
		}

		err = NULL;
		n = udp_query_opt("127.0.0.1", p8s, "PING", 4, resp, sizeof(resp), 3, NULL, NULL, NULL, &err);
		check("query survives a signal mid-wait (EINTR retried)", n == 4);
		check("the interrupting signal was actually delivered", eintr_caught != 0);
		waitpid(srvk, &st, 0); waitpid(sigk, &st, 0);
		sigaction(SIGUSR1, &old, NULL);
	}

	/* 9. Source-address binding (opts.srcip): a query bound to an explicit local
	 *    address still round-trips, and an unusable source (here an IPv6 literal
	 *    against an IPv4 server) fails cleanly rather than silently ignoring the
	 *    setting - the multihomed/ACL contract the ntp host@srcip option relies on. */
	{
		int s9, p9; char p9s[16]; pid_t k9; int st;
		udp_query_opts o;

		s9 = bind_loopback(&p9);
		if (s9 < 0) { fprintf(stderr, "SKIP: cannot bind loopback UDP socket\n"); return SKIP; }
		snprintf(p9s, sizeof(p9s), "%d", p9);
		k9 = fork();
		if (k9 < 0) { fprintf(stderr, "SKIP: fork failed\n"); close(s9); return SKIP; }
		if (k9 == 0) {
			struct sockaddr_in cli; socklen_t cl = sizeof(cli); char buf[64];
			int m = recvfrom(s9, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cl);
			if (m > 0) sendto(s9, buf, m, 0, (struct sockaddr *)&cli, cl);
			close(s9); _exit(0);
		}
		close(s9);

		/* 9a: bind the loopback source explicitly - round-trip still works. */
		memset(&o, 0, sizeof(o)); o.srcip = "127.0.0.1";
		err = NULL;
		n = udp_query_opt("127.0.0.1", p9s, "BIND", 4, resp, sizeof(resp), 3, &o, NULL, NULL, &err);
		check("bound source round-trip returns 4 bytes", n == 4);
		check("bound source payload matches", (n == 4) && (memcmp(resp, "BIND", 4) == 0));
		waitpid(k9, &st, 0);

		/* 9b: a source in the wrong address family is rejected, not ignored. The
		 *     bind getaddrinfo fails before any datagram leaves, so this is a fast
		 *     error, not a timeout. */
		memset(&o, 0, sizeof(o)); o.srcip = "::1";	/* IPv6 source, IPv4 server */
		err = NULL;
		n = udp_query_opt("127.0.0.1", p9s, "X", 1, resp, sizeof(resp), 1, &o, NULL, NULL, &err);
		check("mismatched source family rejected (-1)", n == -1);
		check("mismatched source reason reported", err != NULL);
	}

	/* NOTE: the wall-clock-step rejection (UDP_CLOCK_STEP_US, exercised on the
	 * timestamped NTP path) is deliberately NOT tested here - forcing a wall-clock
	 * step needs settimeofday()/CAP_SYS_TIME, which a test must not do. A conn_*
	 * port must keep that check; it is guarded by inspection, not by this harness. */

	if (failures) { fprintf(stderr, "\n%d check(s) failed\n", failures); return 1; }
	printf("\nall udp_query checks passed\n");
	return 0;
}
