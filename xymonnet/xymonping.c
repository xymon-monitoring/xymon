/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* This is an implementation of a fast "ping" program, for use with Xymon.    */
/*                                                                            */
/* Copyright (C) 2006-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include <net/if.h>		/* if_nametoindex(), for a link-local address given as addr%iface */
#include <netdb.h>

#include <stdio.h>

#include "libxymon.h"

#define PING_PACKET_SIZE 64
#define PING_MINIMUM_SIZE ICMP_MINLEN
#define SENDLIMIT 100

typedef struct hostdata_t {
	int id;
	int family;			/* AF_INET or AF_INET6 */
	struct sockaddr_storage addr;	/* Address of host to ping, of either family */
	socklen_t addrlen;
	char *name;			/* The address as given; an IPv6 result is printed with it */
	int received;			/* how many ICMP_ECHO replies we've got */
	struct timespec rtt_total;
	struct hostdata_t *next;
} hostdata_t;

hostdata_t *hosthead = NULL;
int hostcount = 0;
hostdata_t **hosts = NULL;	/* Array of pointers to the hostdata records, for fast access via ID */
int myicmpid;
int senddelay = (1000000 / 50);	/* Delay between sending packets, in microseconds */
int pingsocket4 = -1;		/* The raw ICMP socket, opened when an IPv4 address is listed */
int pingsocket6 = -1;		/* The raw ICMPv6 socket, opened when an IPv6 address is listed */

/* The socket a host is pinged on: -1 when its family's socket could not be opened */
static int host_socket(hostdata_t *h)
{
	return (h->family == AF_INET6) ? pingsocket6 : pingsocket4;
}

/* A printable form of a host's address, for debugging; a static buffer, not reentrant */
static const char *addr_str(struct sockaddr_storage *ss)
{
	static char buf[INET6_ADDRSTRLEN];

	if (ss->ss_family == AF_INET6)
		inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ss)->sin6_addr, buf, sizeof(buf));
	else
		inet_ntop(AF_INET, &((struct sockaddr_in *)ss)->sin_addr, buf, sizeof(buf));
	return buf;
}

/*
 * How a result names its host. An IPv4 address is printed in dotted form, as
 * it always was. An IPv6 address is printed as it was given: xymonnet matches
 * a result to its hosts.cfg line by the text, and inet_ntop() would shorten
 * "0:0:0:0:0:0:0:1" to "::1" and drop a link-local %interface.
 */
static const char *host_label(hostdata_t *h)
{
	return (h->family == AF_INET6) ? h->name : addr_str(&h->addr);
}

/*
 * Parse one address into h: IPv4 with inet_aton(), as it always was, so the
 * short forms it accepts ("10.1") keep working; anything else as IPv6 with
 * inet_pton(), a link-local address with its interface after a '%'.
 * Returns 1 when h is filled, 0 when l is neither, and -1 when l named an
 * interface this host does not have, which is reported here. l is modified.
 */
static int parse_address(char *l, hostdata_t *h)
{
	struct sockaddr_in *sin4 = (struct sockaddr_in *)&h->addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&h->addr;
	char *pct = strchr(l, '%');
	unsigned int scope_id = 0;

	if (!pct && (inet_aton(l, &sin4->sin_addr) != 0)) {
		h->family = AF_INET;
		sin4->sin_family = AF_INET;
		sin4->sin_port = 0;
		h->addrlen = sizeof(struct sockaddr_in);
		return 1;
	}

	if (pct) {
		*pct = '\0';
		scope_id = if_nametoindex(pct + 1);
		if (scope_id == 0) {
			errprintf("Dropping %s%%%s - unknown interface\n", l, pct + 1);
			*pct = '%';
			return -1;
		}
	}
	if (inet_pton(AF_INET6, l, &sin6->sin6_addr) != 1) {
		if (pct) *pct = '%';
		return 0;
	}
	if (pct) *pct = '%';
	h->family = AF_INET6;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = 0;
	sin6->sin6_scope_id = scope_id;
	h->addrlen = sizeof(struct sockaddr_in6);
	return 1;
}

/* This routine more or less taken from the "fping" source. Apparently by W. Stevens (public domain) */
int calc_icmp_checksum(unsigned short *pkt, int pktlen)
{
	unsigned short result;
	long sum = 0;
	unsigned short extrabyte = 0;

	while (pktlen > 1) {
		sum += *pkt++;
		pktlen -= 2;
	}

	if (pktlen == 1) {
		*(unsigned char *)(&extrabyte) = *(unsigned char *)pkt;
		sum += extrabyte;
	}

	sum = ( sum >> 16 ) + ( sum & 0xffff ); /* add hi 16 to low 16 */
	sum += ( sum >> 16 );			/* add carry */
	result = ~sum;				/* ones-complement, truncate*/

	return result;
}


char *nextip(int argc, char *argv[], FILE *fd)
{
	static int argi = 0;
	static int cmdmode = 0;
	static char buf[4096];

	if (argi == 0) {
		/* Check if there are any command-line IP's, of either family */
		hostdata_t probe;

		for (argi=1; (argi < argc); argi++) {
			char word[INET6_ADDRSTRLEN + IF_NAMESIZE + 1];

			if (strlen(argv[argi]) >= sizeof(word)) continue;
			strcpy(word, argv[argi]);
			if (parse_address(word, &probe) == 1) break;
		}
		cmdmode = (argi < argc);
	}

	if (cmdmode) {
		/* Skip any options in-between the IP's */
		while ((argi < argc) && (*(argv[argi]) == '-')) argi++;
		if (argi < argc) {
			argi++;
			return argv[argi-1];
		}
	}
	else {
		if (fgets(buf, sizeof(buf), fd)) {
			char *p;
			p = strchr(buf, '\n'); if (p) *p = '\0';
			return buf;
		}
	}

	return NULL;
}

void load_ips(int argc, char *argv[], FILE *fd)
{
	char *l;
	hostdata_t *tail = NULL;
	hostdata_t *walk;
	int i;

	while ((l = nextip(argc, argv, fd)) != NULL) {
		hostdata_t *newitem;

		/* An address ends at the first blank: inet_pton() refuses trailing text */
		l[strcspn(l, " \t\r")] = '\0';
		if (strlen(l) == 0) continue;

		newitem = (hostdata_t *)calloc(1, sizeof(hostdata_t));
		newitem->name = strdup(l);

		switch (parse_address(l, newitem)) {
		  case 1: break;
		  case 0: errprintf("Dropping %s - not an IP\n", newitem->name);
			/* fall through */
		  default:
			xfree(newitem->name);
			xfree(newitem);
			continue;
		}

		if (tail) {
			tail->next = newitem;
		}
		else {
			hosthead = newitem;
		}
		hostcount++;
		tail = newitem;
	}

	/* Setup the table of hostdata records */
	hosts = (hostdata_t **)malloc((hostcount+1) * sizeof(hostdata_t *));
	for (i=0, walk=hosthead; (walk); walk=walk->next, i++) hosts[i] = walk;
	hosts[hostcount] = NULL;
}


/* This is the data we send with each ping packet */
typedef struct pingdata_t {
	int id;				/* ID for the host this belongs to */
	struct timespec timesent;	/* time we sent this ping */
} pingdata_t;


int send_ping(int startidx, int minresponses)
{
	static unsigned char buffer[PING_PACKET_SIZE];
	struct icmp *icmphdr;
	struct icmp6_hdr *icmp6hdr;
	struct pingdata_t *pingdata;
	int sentbytes, sock;
	int idx = startidx;

	/*
	 * Sends one ICMP "echo-request" packet.
	 *
	 * Note: A first attempt at this kept sending packets until
	 * we got an EWOULDBLOCK or a send error. This causes incoming
	 * responses to be dropped.
	 */

	/* Skip the hosts that have already delivered a ping response, and those whose family has no socket */
	while ((idx < hostcount) && ((hosts[idx]->received >= minresponses) || (host_socket(hosts[idx]) == -1))) idx++;
	if (idx >= hostcount) return hostcount;
	sock = host_socket(hosts[idx]);

	/* 
	 * Don't flood the net.
	 * By enforcing a brief sleep here, we force a delay
	 * between sending packets. It is easiest to do before sending
	 * a packet, because if done after the send completes, then
	 * it affects the RTT measurements.
	 */
	if (senddelay) usleep(senddelay);

	/* Build the packet and send it */
	memset(buffer, 0, PING_PACKET_SIZE);

	if (hosts[idx]->family == AF_INET6) {
		/* The kernel computes the ICMPv6 checksum on a raw socket */
		icmp6hdr = (struct icmp6_hdr *)buffer;
		icmp6hdr->icmp6_type = ICMP6_ECHO_REQUEST;
		icmp6hdr->icmp6_code = 0;
		icmp6hdr->icmp6_cksum = 0;
		icmp6hdr->icmp6_seq = htons(idx+1);	/* So we can map response to our hosts */
		icmp6hdr->icmp6_id = htons(myicmpid);

		pingdata = (struct pingdata_t *)(buffer + sizeof(struct icmp6_hdr));
		pingdata->id = idx;
		getntimer(&pingdata->timesent);
	}
	else {
		icmphdr = (struct icmp *)buffer;
		icmphdr->icmp_type = ICMP_ECHO;
		icmphdr->icmp_code = 0;
		icmphdr->icmp_cksum = 0;
		icmphdr->icmp_seq = htons(idx+1);	/* So we can map response to our hosts */
		icmphdr->icmp_id = htons(myicmpid);

		pingdata = (struct pingdata_t *)(buffer + sizeof(struct icmp));
		pingdata->id = idx;
		getntimer(&pingdata->timesent);

		icmphdr->icmp_cksum = calc_icmp_checksum((unsigned short *)buffer, PING_PACKET_SIZE);
	}

	sentbytes = sendto(sock, buffer, PING_PACKET_SIZE, 0, 
			   (struct sockaddr *) &hosts[idx]->addr, hosts[idx]->addrlen);

	if (sentbytes == -1) {
		if (errno != EWOULDBLOCK) {
			errprintf("Failed to send ICMP packet: %s\n", strerror(errno));
			idx++; /* To avoid looping indefinitely trying to send to this host */
		}
	}
	else if (sentbytes == PING_PACKET_SIZE) {
		/* We managed to send a ping! */
		if (debug) {
			dbgprintf("Sent a ping to %s: index=%d, id=%d\n",
				addr_str(&hosts[idx]->addr), idx, myicmpid);
		}
		idx++;
	}

	return idx;
}


int get_response(int sock, int family)
{
	static unsigned char buffer[4096];
	struct sockaddr_storage addr;
	int n, pktcount;
	unsigned int addrlen;
	struct ip *iphdr;
	int iphdrlen, icmphdrlen;
	struct icmp *icmphdr;
	struct icmp6_hdr *icmp6hdr;
	int reply_type, reply_id, echoreply;
	struct pingdata_t *pingdata;
	int hostidx;
	struct timespec rtt;

	/*
	 * Read responses from the network.
	 * We know (because select() told us) that there is some data
	 * to read. To avoid losing packets, read as much as we can.
	 */
	pktcount = 0;
	do {
		addrlen = sizeof(addr);
		n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &addrlen);
		if (n < 0) {
			if (errno != EWOULDBLOCK) errprintf("Failed to receive packet: %s\n", strerror(errno));
			continue;
		}

		getntimer(&rtt);

		/*
		 * Get the ICMP header, and our host index which is the sequence number.
		 * Thanks to "fping" for this neat way of matching requests and responses.
		 *
		 * An IPv4 raw socket delivers the IP header in front of the ICMP
		 * header; an ICMPv6 raw socket delivers the ICMPv6 header alone.
		 */
		if (family == AF_INET6) {
			iphdrlen = 0;
			icmphdrlen = sizeof(struct icmp6_hdr);
			if (n < icmphdrlen) {
				errprintf("Short packet ignored\n");
				continue;
			}
			icmp6hdr = (struct icmp6_hdr *)buffer;
			reply_type = icmp6hdr->icmp6_type;
			reply_id = ntohs(icmp6hdr->icmp6_id);
			hostidx = ntohs(icmp6hdr->icmp6_seq)-1;
			echoreply = (reply_type == ICMP6_ECHO_REPLY);
		}
		else {
			/* Check the IP header - we need to have at least enough bytes for an ICMP header. */
			iphdr = (struct ip *)buffer;
			iphdrlen = (iphdr->ip_hl << 2);	/* IP header always aligned on 4-byte boundary */
			icmphdrlen = sizeof(struct icmp);
			if (n < (iphdrlen + PING_MINIMUM_SIZE)) {
				errprintf("Short packet ignored\n");
				continue;
			}
			icmphdr = (struct icmp *)(buffer + iphdrlen);
			reply_type = icmphdr->icmp_type;
			reply_id = ntohs(icmphdr->icmp_id);
			hostidx = ntohs(icmphdr->icmp_seq)-1;
			echoreply = (reply_type == ICMP_ECHOREPLY);
		}

		if (debug) {
			dbgprintf("Got packet from %s: type=%d, index=%d, id=%d\n",
				addr_str(&addr), reply_type, reply_id, hostidx);
		}

		if (echoreply) {
			if (reply_id != myicmpid) {
				/* Not one of our packets. Happens if someone else does ping simultaneously. */
				continue;
			}

			/* A reply is counted for a host of its own family only, and only when it carries our payload */
			if ((hostidx >= 0) && (hostidx < hostcount) && (hosts[hostidx]->family == family)
			    && (n >= (int)(iphdrlen + icmphdrlen + sizeof(struct pingdata_t)))) {
				/* Looks like one of our packets succeeded. */
				pktcount++;
				hosts[hostidx]->received += 1;

				pingdata = (struct pingdata_t *)(buffer + iphdrlen + icmphdrlen);

				/* Calculate the round-trip time. */
				rtt.tv_sec -= pingdata->timesent.tv_sec;
				rtt.tv_nsec -= pingdata->timesent.tv_nsec;
				if (rtt.tv_nsec < 0) {
					rtt.tv_sec--;
					rtt.tv_nsec += 1000000000;
				}

				/* Add RTT to the total time */
				hosts[hostidx]->rtt_total.tv_sec += rtt.tv_sec;
				hosts[hostidx]->rtt_total.tv_nsec += rtt.tv_nsec;
				if (hosts[hostidx]->rtt_total.tv_nsec >= 1000000000) {
					hosts[hostidx]->rtt_total.tv_sec++;
					hosts[hostidx]->rtt_total.tv_nsec -= 1000000000;
				}
			}
			continue;
		}

		if (family == AF_INET6) {
			/*
			 * The ICMPv6 socket is filtered down to echo replies where
			 * ICMP6_FILTER exists; elsewhere neighbour discovery and
			 * router advertisements arrive here too, and are not errors.
			 */
			continue;
		}

		switch (reply_type) {
		  case ICMP_ECHO:
			/* Sometimes, we see our own packets going out (if we ping ourselves) */
			break;

		  case ICMP_UNREACH:
			/* We simply ignore these. Hosts get retried until we succeed, then reported as down. */
			break;

		  case ICMP_REDIRECT:
			/* Ignored - the IP stack handles this. */
			break;

		  default:
			/* Shouldn't happen */
			errprintf("Got a packet that wasn't a reply - type %d\n", reply_type);
			break;
		}
	} while (n > 0);

	return pktcount;
}

int count_pending(int minresponses)
{
	int result = 0;
	int idx;

	/* Counts how many hosts we haven't seen a reply from yet. */
	for (idx = 0; (idx < hostcount); idx++)
		if (hosts[idx]->received < minresponses) result++;

	return result;
}

void show_results(void)
{
	int idx;
	unsigned long rtt_usecs; /* Big enough for 2147 seconds - larger than we will ever see */

	/*
	 * Print out the results. Format is identical to "fping -Ae" so we can use
	 * it directly in Xymon without changing the xymonnet code.
	 */
	for (idx = 0; (idx < hostcount); idx++) {
		if (hosts[idx]->received > 0) {
			printf("%s is alive", host_label(hosts[idx]));
			rtt_usecs = (hosts[idx]->rtt_total.tv_sec*1000000 + (hosts[idx]->rtt_total.tv_nsec / 1000)) / hosts[idx]->received;
			if (rtt_usecs >= 3000) {
				printf(" (%.1f ms)\n", rtt_usecs / 1000.0);
			}
			else {
				printf(" (%lu usec)\n", rtt_usecs);
			}
		}
		else {
			printf("%s is unreachable\n", host_label(hosts[idx]));
		}
	}
}

/*
 * Open the raw socket of one family, as root, and bind it to the source
 * address when one of that family was given. Returns -1 with the error
 * reported when the socket cannot be had; a failed bind is reported and
 * the socket kept, as before.
 */
static int open_ping_socket(int family, struct sockaddr *src, socklen_t srclen, char *srcip)
{
	struct protoent *proto;
	int protonumber, sock, sockerr = 0, binderr = 0;

	if (family == AF_INET6) {
		protonumber = IPPROTO_ICMPV6;
	}
	else {
		proto = getprotobyname("icmp");
		protonumber = (proto ? proto->p_proto : IPPROTO_ICMP);
	}

	/* Get a raw socket. Requires root privs. */
	get_root();
	sock = socket(family, SOCK_RAW, protonumber); sockerr = errno;
	if ((sock != -1) && src) {
		/* Bind to a specific source address */
		if (bind(sock, src, srclen) == -1) binderr = errno;
	}
	drop_root();

	if (sock == -1) {
		errprintf("Cannot get RAW %s socket: %s\n", ((family == AF_INET6) ? "ICMPv6" : "ICMP"), strerror(sockerr));
		if (sockerr == EPERM) errprintf("This program must be installed suid-root\n");
		return -1;
	}

	if (binderr != 0) {
		errprintf("Cannot bind to source address %s: %s\nUsing default address\n",
			  srcip, strerror(binderr));
	}

#ifdef ICMP6_FILTER
	if (family == AF_INET6) {
		/* Only echo replies are wanted; without the filter every ICMPv6 packet the host sees arrives here */
		struct icmp6_filter filter;

		ICMP6_FILTER_SETBLOCKALL(&filter);
		ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &filter);
		if (setsockopt(sock, IPPROTO_ICMPV6, ICMP6_FILTER, &filter, sizeof(filter)) == -1) {
			errprintf("Cannot set ICMPv6 filter: %s\n", strerror(errno));
		}
	}
#endif

	/* Set the socket non-blocking - we use select() exclusively */
	fcntl(sock, F_SETFL, O_NONBLOCK);

	return sock;
}

int main(int argc, char *argv[])
{
	int argi, sendidx, pending, minresponses = 1, tries = 3, timeout = 5;
	int need4 = 0, need6 = 0, maxfd;
	char *srcip = NULL;
	struct sockaddr_in src_addr;
	struct sockaddr_in6 src_addr6;
	struct sockaddr *src4 = NULL, *src6 = NULL;

	/* Immediately drop all root privileges. */
	drop_root();

	for (argi = 1; (argi < argc); argi++) {
		if (strncmp(argv[argi], "--retries=", 10) == 0) {
			char *delim = strchr(argv[argi], '=');
			tries = 1 + atoi(delim+1);
		}
		else if (strncmp(argv[argi], "--timeout=", 10) == 0) {
			char *delim = strchr(argv[argi], '=');
			timeout = atoi(delim+1);
		}
		else if (strncmp(argv[argi], "--responses=", 11) == 0) {
			char *delim = strchr(argv[argi], '=');
			minresponses = atoi(delim+1);
		}
		else if (strncmp(argv[argi], "--source=", 9) == 0) {
			char *delim = strchr(argv[argi], '=');
			srcip = strdup(delim+1);
		}
		else if (strncmp(argv[argi], "--max-pps=", 10) == 0) {
			char *delim = strchr(argv[argi], '=');
			senddelay = (1000000 / atoi(delim+1));
		}
		else if (strncmp(argv[argi], "--debug", 7) == 0) {
			char *delim = strchr(argv[argi], '=');
			debug = 1;
			if (delim) set_debugfile(delim+1, 0);
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			fprintf(stderr, "%s [--retries=N] [--timeout=N] [--responses=N] [--max-pps=N] [--source=IP] [IP ...]\n", argv[0]);
			fprintf(stderr, "Reads IPv4 or IPv6 addresses from stdin, one per line, unless given as arguments.\n");
			return 0;
		}

		/* fping compatibility options */
		else if (strncmp(argv[argi], "-i", 2) == 0) {
			char *val = argv[argi] + 2;
			if (isdigit((int) *val)) senddelay = atoi(val);
		}
		else if (strncmp(argv[argi], "-r", 2) == 0) {
			char *val = argv[argi] + 2;
			if (isdigit((int) *val)) tries = atoi(val);
		}
		else if (strncmp(argv[argi], "-S", 2) == 0) {
			char *val = argv[argi] + 2;
			srcip = strdup(val);
		}
		else if (*(argv[argi]) == '-') {
			/* Ignore everything else - for fping compatibility */
		}
	}

	if (srcip != NULL) {
		/* Setup the source address: it binds the socket of its own family */
		memset(&src_addr, 0, sizeof(src_addr));
		memset(&src_addr6, 0, sizeof(src_addr6));
		if (inet_pton(AF_INET6, srcip, &src_addr6.sin6_addr) == 1) {
			src_addr6.sin6_family = AF_INET6;
			src6 = (struct sockaddr *)&src_addr6;
		}
		else {
			src_addr.sin_family = AF_INET;
			src_addr.sin_addr.s_addr = inet_addr(srcip);
			src_addr.sin_port = htons(0);
			src4 = (struct sockaddr *)&src_addr;
		}
	}

	/* The addresses are read unprivileged; which sockets are needed follows from them */
	load_ips(argc, argv, stdin);
	for (argi = 0; (argi < hostcount); argi++) {
		if (hosts[argi]->family == AF_INET6) need6 = 1; else need4 = 1;
	}

	/*
	 * A family whose socket cannot be opened is reported and its hosts
	 * come out unreachable; the other family is still pinged. Only a run
	 * with no socket at all ends here, with the exit status it always had.
	 */
	if (need4) pingsocket4 = open_ping_socket(AF_INET, src4, sizeof(src_addr), srcip);
	if (need6) pingsocket6 = open_ping_socket(AF_INET6, src6, sizeof(src_addr6), srcip);
	if ((need4 || need6) && (pingsocket4 == -1) && (pingsocket6 == -1)) return 3;
	maxfd = (pingsocket4 > pingsocket6) ? pingsocket4 : pingsocket6;

	pending = count_pending(minresponses);

	while (tries) {
		int sendnow = SENDLIMIT;
		time_t cutoff = getcurrenttime(NULL) + timeout + 1;
		sendidx = 0;

		/* Change this on each iteration, so we don't mix packets from each round of pings */
		myicmpid = ((getpid()+tries) & 0x7FFF);

		/* Do one loop over the hosts we havent had responses from yet. */
		while (pending > 0) {
			fd_set readfds, writefds;
			struct timeval selecttmo;
			int n, nextidx, sendsock = -1;

			FD_ZERO(&readfds);
			FD_ZERO(&writefds);
			if (pingsocket4 != -1) FD_SET(pingsocket4, &readfds);
			if (pingsocket6 != -1) FD_SET(pingsocket6, &readfds);

			/* Wait to write on the socket of the next host to ping, as send_ping() will pick it */
			nextidx = sendidx;
			while ((nextidx < hostcount) && ((hosts[nextidx]->received >= minresponses) || (host_socket(hosts[nextidx]) == -1))) nextidx++;
			if (nextidx < hostcount) sendsock = host_socket(hosts[nextidx]); else sendidx = hostcount;
			if (sendnow && (sendsock != -1)) FD_SET(sendsock, &writefds);

			selecttmo.tv_sec = 0;
			selecttmo.tv_usec = 100000;
			n = select(maxfd+1, &readfds, &writefds, NULL, &selecttmo);

			if (n < 0) {
				if (errno == EINTR) continue;
				errprintf("select failed: %s\n", strerror(errno));
				return 4;
			}
			else if (n == 0) {
				/* Time out */
				sendnow = SENDLIMIT;
			}
			else {
				if (sendnow && (sendsock != -1) && FD_ISSET(sendsock, &writefds)) {
					/* OK to send */
					sendidx = send_ping(sendidx, minresponses);
					sendnow--;

					/* Adjust the cutoff time, so we wait TIMEOUT seconds for a response */
					cutoff = getcurrenttime(NULL) + timeout + 1;
				}

				if ((pingsocket4 != -1) && FD_ISSET(pingsocket4, &readfds)) {
					/* Grab the replies */
					int count = get_response(pingsocket4, AF_INET);
					pending -= count;
					sendnow += count; if (sendnow > SENDLIMIT) sendnow = SENDLIMIT;
				}
				if ((pingsocket6 != -1) && FD_ISSET(pingsocket6, &readfds)) {
					int count = get_response(pingsocket6, AF_INET6);
					pending -= count;
					sendnow += count; if (sendnow > SENDLIMIT) sendnow = SENDLIMIT;
				}
			}

			/* See if we have hit the timeout */
			if ((getcurrenttime(NULL) >= cutoff) && (sendidx >= hostcount)) {
				pending = 0;
			}
		}

		tries--;
		pending = count_pending(minresponses);
		if (pending == 0) {
			/* Have got responses for all hosts - we're done */
			tries = 0;
		}
	}

	if (pingsocket4 != -1) close(pingsocket4);
	if (pingsocket6 != -1) close(pingsocket6);

	show_results();

	return 0;
}

