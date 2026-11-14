/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/xymonnet/xymonping-harness.c
 *
 * Drives xymonping's reply parser, get_response(), with crafted packets: it
 * #includes xymonnet/xymonping.c (main renamed) and hands get_response() one
 * end of a socketpair as the ICMP socket, so each rule is checked without
 * root, a network or timing. Run by tests/xymonnet/xymonping-unit.sh.
 *
 * Pins, for an IPv4 raw socket and for an unprivileged SOCK_DGRAM one:
 *   - the IP header is skipped when present -- always on a raw socket, and on
 *     a SOCK_DGRAM socket only when the packet starts with version 4 (macOS
 *     keeps it, Linux does not);
 *   - a raw socket takes only replies carrying our icmp_id; a SOCK_DGRAM one,
 *     whose icmp_id the kernel sets, only those whose payload carries it;
 *   - main() takes the unprivileged socket when offered and falls back to the
 *     raw one when it is refused, for IPv4 and for IPv6, asking each time for
 *     the family's ICMP protocol, and every ping it sends carries our id in
 *     its payload;
 *   - for ICMPv6, which has no IP header on either socket, the same rules: a
 *     raw socket takes only our icmp_id, a SOCK_DGRAM one only our payload;
 *   - a SOCK_DGRAM reply too short to hold the payload, a packet that is not
 *     an echo reply, and a sequence number outside the host list are ignored.
 */

/* What xymonping sends is recorded instead of leaving the box, and the
 * sockets it opens are socketpair ends: sendto() and socket() are
 * redirected before xymonping.c is included. fail_dgram makes the
 * unprivileged SOCK_DGRAM socket fail, so the raw fallback is taken. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define MAXSENT 64
static unsigned char sent_pkt[MAXSENT][64];
static int nsent = 0;
static int fail_dgram = 0;

static ssize_t harness_sendto(int s, const void *b, size_t n, int f, const struct sockaddr *a, socklen_t l)
{
	(void)s; (void)f; (void)a; (void)l;
	if (nsent < MAXSENT) memcpy(sent_pkt[nsent], b, (n < 64) ? n : 64);
	nsent++;
	return (ssize_t)n;
}
#define sendto harness_sendto

/* Every socket() call, with its arguments, so the family, type and protocol
 * xymonping asks for are checked, not only the order it asks in. */
#define MAXSOCK 8
static int sock_args[MAXSOCK][3];
static int nsock = 0;

static int harness_socket(int domain, int type, int proto)
{
	int p[2];

	if (nsock < MAXSOCK) {
		sock_args[nsock][0] = domain;
		sock_args[nsock][1] = type;
		sock_args[nsock][2] = proto;
	}
	nsock++;
	if (fail_dgram && (type == SOCK_DGRAM)) { errno = EACCES; return -1; }
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, p) != 0) return -1;
	return p[0];
}
#define socket harness_socket

#define main xymonping_main
#include "xymonping.c"
#undef main

static int fails = 0;
static int sv[2];

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s\n", what); fails++; } } while (0)
/* main()'s report, the last line of the child's output after what main() printed, against the one expected */
static int last_line_is(const char *o, const char *w)
{
	size_t lo = strlen(o), lw = strlen(w);

	return (lo >= lw) && (strcmp(o + lo - lw, w) == 0) && ((lo == lw) || (o[lo - lw - 1] == '\n'));
}
#define CHECK_OUT(what) do { if (!last_line_is(out, want)) { printf("FAIL %s\n  expected last line: %s  got:\n%s", what, want, out); fails++; } } while (0)

/* Load a newline-separated address list through load_ips(). */
static void load(const char *list)
{
	char *argv[] = { "xymonping", NULL };
	FILE *fd = tmpfile();

	fputs(list, fd);
	rewind(fd);
	hosthead = NULL; hosts = NULL; hostcount = 0;
	load_ips(1, argv, fd);
	fclose(fd);
}

/* Queue one ICMP packet on the socket get_response() reads. */
static void reply(int iphdr, int type, int hdr_id, int seq, int payload_id, int payload_icmpid, int cut)
{
	unsigned char buf[256];
	int off = (iphdr ? 20 : 0);
	struct icmp *icmp = (struct icmp *)(buf + off);
	struct pingdata_t pd;

	memset(buf, 0, sizeof(buf));
	memset(&pd, 0, sizeof(pd));
	if (iphdr) buf[0] = 0x45;	/* IPv4, 20-byte header */
	icmp->icmp_type = type;
	icmp->icmp_id   = htons(hdr_id);
	icmp->icmp_seq  = htons(seq);
	pd.id     = payload_id;
	pd.icmpid = payload_icmpid;
	getntimer(&pd.timesent);
	memcpy(buf + off + sizeof(struct icmp), &pd, sizeof(pd));
	if (send(sv[1], buf, off + sizeof(struct icmp) + sizeof(struct pingdata_t) - cut, 0) < 0) {
		perror("send"); exit(2);
	}
}

/* Feed one packet to host 0 and return how many replies it was credited. */
static int credited(int dgram, int iphdr, int type, int hdr_id, int seq, int payload_id, int payload_icmpid, int cut)
{
	is_dgram4 = dgram;
	hosts[0]->received = 0;
	reply(iphdr, type, hdr_id, seq, payload_id, payload_icmpid, cut);
	(void)get_response(sv[0], AF_INET);
	return hosts[0]->received;
}

/* The same for an ICMPv6 reply, which never has an IP header in front. */
static int credited6(int dgram, int type, int hdr_id, int seq, int payload_icmpid, int cut)
{
	unsigned char buf[256];
	struct icmp6_hdr *icmp6 = (struct icmp6_hdr *)buf;
	struct pingdata_t pd;

	is_dgram6 = dgram;
	hosts[0]->received = 0;
	memset(buf, 0, sizeof(buf));
	memset(&pd, 0, sizeof(pd));
	icmp6->icmp6_type = type;
	icmp6->icmp6_id   = htons(hdr_id);
	icmp6->icmp6_seq  = htons(seq);
	pd.icmpid = payload_icmpid;
	getntimer(&pd.timesent);
	memcpy(buf + sizeof(struct icmp6_hdr), &pd, sizeof(pd));
	if (send(sv[1], buf, sizeof(struct icmp6_hdr) + sizeof(struct pingdata_t) - cut, 0) < 0) {
		perror("send"); exit(2);
	}
	(void)get_response(sv[0], AF_INET6);
	return hosts[0]->received;
}

/* Run main() in a child with argv and stdin from /dev/null; the child
 * reports, after main() returns, which kind of socket it used and what it
 * sent. Returns the child's exit status, its stdout in out. */
static int run_main(char **argv, char *out, size_t outlen)
{
	int p[2], status, argc = 0;
	pid_t pid;
	ssize_t n, got = 0;

	while (argv[argc]) argc++;
	fflush(stdout);	/* or the child inherits, and repeats, what the parent has not written yet */
	if (pipe(p) != 0) return -1;
	pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_RDWR), i, ours = 1;
		int v6 = (strchr(argv[argc - 1], ':') != NULL);

		dup2(devnull, 0);
		dup2(p[1], 1);
		dup2(devnull, 2);	/* the raw ICMPv6 filter cannot be set on a socketpair; that is not what is checked */
		close(p[0]);
		status = xymonping_main(argc, argv);
		for (i = 0; (i < nsent) && (i < MAXSENT); i++) {
			/* The echo identifier sits at the same offset in both headers; the payload follows each header */
			struct icmp *icmp = (struct icmp *)sent_pkt[i];
			struct pingdata_t pd;
			memcpy(&pd, sent_pkt[i] + (v6 ? sizeof(struct icmp6_hdr) : sizeof(struct icmp)), sizeof(pd));
			if (pd.icmpid != ntohs(icmp->icmp_id)) ours = 0;
		}
		printf("is_dgram=%d is_dgram6=%d sent=%d payload_carries_id=%d", is_dgram4, is_dgram6, nsent, ours);
		for (i = 0; (i < nsock) && (i < MAXSOCK); i++) {
			printf(" socket=%s/%s/%d",
			       ((sock_args[i][0] == AF_INET6) ? "inet6" : ((sock_args[i][0] == AF_INET) ? "inet" : "other")),
			       ((sock_args[i][1] == SOCK_DGRAM) ? "dgram" : ((sock_args[i][1] == SOCK_RAW) ? "raw" : "other")),
			       sock_args[i][2]);
		}
		printf("\n");
		fflush(stdout);
		_exit(status);
	}
	close(p[1]);
	while ((got < (ssize_t)outlen - 1) && ((n = read(p[0], out + got, outlen - 1 - got)) > 0)) got += n;
	out[got] = '\0';
	close(p[0]);
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(void)
{
	char out[1024], want[128];
	char a0[] = "xymonping", a1[] = "--timeout=1", a2[] = "--retries=0", a3[] = "127.0.0.1", a6[] = "::1";
	char *argv1[] = { a0, a1, a2, a3, NULL };
	char *argv6[] = { a0, a1, a2, a6, NULL };

	/* main(): the unprivileged socket first, the raw one when it is
	 * refused, each with the family's ICMP protocol, and every ping
	 * carrying our id in its payload. Done first, in fresh children:
	 * nextip() keeps its state in statics. */
	fail_dgram = 0;
	CHECK(run_main(argv1, out, sizeof(out)) == 0, "main() runs with the unprivileged socket");
	snprintf(want, sizeof(want), "is_dgram=1 is_dgram6=0 sent=1 payload_carries_id=1 socket=inet/dgram/%d\n", IPPROTO_ICMP);
	CHECK_OUT("IPv4: the unprivileged ICMP socket is used when offered, alone, and the ping carries our id");
	fail_dgram = 1;
	CHECK(run_main(argv1, out, sizeof(out)) == 0, "main() runs when the unprivileged socket is refused");
	snprintf(want, sizeof(want), "is_dgram=0 is_dgram6=0 sent=1 payload_carries_id=1 socket=inet/dgram/%d socket=inet/raw/%d\n",
		 IPPROTO_ICMP, IPPROTO_ICMP);
	CHECK_OUT("IPv4: the raw ICMP socket is used when the unprivileged one is refused");
	fail_dgram = 0;
	CHECK(run_main(argv6, out, sizeof(out)) == 0, "main() runs with the unprivileged ICMPv6 socket");
	snprintf(want, sizeof(want), "is_dgram=0 is_dgram6=1 sent=1 payload_carries_id=1 socket=inet6/dgram/%d\n", IPPROTO_ICMPV6);
	CHECK_OUT("IPv6: the unprivileged ICMPv6 socket is used when offered, alone, and the ping carries our id");
	fail_dgram = 1;
	CHECK(run_main(argv6, out, sizeof(out)) == 0, "main() runs when the unprivileged ICMPv6 socket is refused");
	snprintf(want, sizeof(want), "is_dgram=0 is_dgram6=0 sent=1 payload_carries_id=1 socket=inet6/dgram/%d socket=inet6/raw/%d\n",
		 IPPROTO_ICMPV6, IPPROTO_ICMPV6);
	CHECK_OUT("IPv6: the raw ICMPv6 socket is used when the unprivileged one is refused");
	fail_dgram = 0;

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0) { perror("socketpair"); return 2; }
	fcntl(sv[0], F_SETFL, O_NONBLOCK);
	myicmpid = 1234;
	load("127.0.0.1\n127.0.0.2\n");
	CHECK(hostcount == 2, "two hosts loaded");

	/* Raw socket: header always there, our icmp_id required. */
	CHECK(credited(0, 1, ICMP_ECHOREPLY, 1234, 1, 0, 1234, 0) == 1, "raw: our reply counts");
	CHECK(credited(0, 1, ICMP_ECHOREPLY, 999, 1, 0, 1234, 0) == 0, "raw: another icmp_id is ignored");

	/* SOCK_DGRAM: the kernel's icmp_id, our id in the payload. */
	CHECK(credited(1, 0, ICMP_ECHOREPLY, 77, 1, 0, 1234, 0) == 1, "dgram without IP header: our reply counts");
	CHECK(credited(1, 1, ICMP_ECHOREPLY, 77, 1, 0, 1234, 0) == 1, "dgram with IP header (macOS): our reply counts");
	CHECK(credited(1, 0, ICMP_ECHOREPLY, 77, 1, 0, 999, 0) == 0, "dgram: another run's payload is ignored");
	CHECK(credited(1, 0, ICMP_ECHOREPLY, 77, 1, 0, 1234, 4) == 0, "dgram: a reply too short for the payload is ignored");

	/* Whatever the socket. */
	CHECK(credited(1, 0, ICMP_ECHO, 77, 1, 0, 1234, 0) == 0, "a packet that is not an echo reply is ignored");
	CHECK(credited(1, 0, ICMP_ECHOREPLY, 77, 0, 0, 1234, 0) == 0, "sequence 0, before the host list, is ignored");
	CHECK(credited(0, 1, ICMP_ECHOREPLY, 1234, 3, 0, 1234, 0) == 0 && hosts[1]->received == 0,
	      "a sequence past the host list is ignored");

	/* ICMPv6, to an IPv6 host: no IP header on either socket. */
	load("::1\n");
	CHECK((hostcount == 1) && (hosts[0]->family == AF_INET6), "one IPv6 host loaded");
	CHECK(credited6(0, ICMP6_ECHO_REPLY, 1234, 1, 1234, 0) == 1, "raw ICMPv6: our reply counts");
	CHECK(credited6(0, ICMP6_ECHO_REPLY, 999, 1, 1234, 0) == 0, "raw ICMPv6: another icmp_id is ignored");
	CHECK(credited6(1, ICMP6_ECHO_REPLY, 77, 1, 1234, 0) == 1, "dgram ICMPv6: our reply counts, whatever the kernel's icmp_id");
	CHECK(credited6(1, ICMP6_ECHO_REPLY, 77, 1, 999, 0) == 0, "dgram ICMPv6: another run's payload is ignored");
	CHECK(credited6(1, ICMP6_ECHO_REPLY, 77, 1, 1234, 4) == 0, "dgram ICMPv6: a reply too short for the payload is ignored");
	CHECK(credited6(1, ICMP6_ECHO_REQUEST, 77, 1, 1234, 0) == 0, "an ICMPv6 packet that is not an echo reply is ignored");

	return fails ? 1 : 0;
}
