/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/xymonnet/xymonping-align-harness.c
 *
 * Sends one ping with xymonping's send_ping() and reads one reply with its
 * get_response(), both from the real xymonnet/xymonping.c (#included, main
 * renamed), over a socketpair -- once for an IPv4 host and once for an IPv6
 * one. Built by tests/xymonnet/xymonping-align.sh with UBSan's alignment
 * check made fatal, so a payload accessed through a misaligned pointer --
 * for IPv4 it sits at offset sizeof(struct icmp) -- aborts the run the way a
 * strict-alignment CPU would.
 */

#define main xymonping_main
#include "xymonping.c"
#undef main

/* One send and one reply for a host of this family; the reply is built as
 * the raw socket delivers it: IPv4 with its IP header, IPv6 without. */
static int round_trip(int sock, int peer, int family)
{
	unsigned char pkt[128];
	struct pingdata_t pd;
	struct hostdata_t host;
	hostdata_t *table[2] = { &host, NULL };
	size_t hdr, len;

	memset(&host, 0, sizeof(host));
	host.family = family;
	hosts = table;
	hostcount = 1;
	if (family == AF_INET) pingsocket4 = sock; else pingsocket6 = sock;

	/* Sending: the payload is written at its offset (sendto() on a
	 * socketpair then fails, which send_ping() reports and survives). */
	(void)send_ping(0, 1);

	memset(pkt, 0, sizeof(pkt));
	if (family == AF_INET) {
		struct icmp *icmp = (struct icmp *)(pkt + 20);

		pkt[0] = 0x45;
		icmp->icmp_type = ICMP_ECHOREPLY;
		icmp->icmp_id   = htons(myicmpid);
		icmp->icmp_seq  = htons(1);
		hdr = 20 + sizeof(struct icmp);
	}
	else {
		struct icmp6_hdr *icmp6 = (struct icmp6_hdr *)pkt;

		icmp6->icmp6_type = ICMP6_ECHO_REPLY;
		icmp6->icmp6_id   = htons(myicmpid);
		icmp6->icmp6_seq  = htons(1);
		hdr = sizeof(struct icmp6_hdr);
	}
	memset(&pd, 0, sizeof(pd));
	getntimer(&pd.timesent);
	memcpy(pkt + hdr, &pd, sizeof(pd));
	len = hdr + sizeof(pd);
	if (send(peer, pkt, len, 0) < 0) { perror("send"); return 2; }
	(void)get_response(sock, family);
	if (host.received != 1) {
		printf("FAIL the %s reply was not counted\n", (family == AF_INET) ? "IPv4" : "IPv6");
		return 1;
	}
	return 0;
}

int main(void)
{
	int sv[2], rc;

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0) { perror("socketpair"); return 2; }
	fcntl(sv[0], F_SETFL, O_NONBLOCK);
	myicmpid = 1234;

	if ((rc = round_trip(sv[0], sv[1], AF_INET)) != 0) return rc;
	return round_trip(sv[0], sv[1], AF_INET6);
}
