/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* This is an implementation of a fast "ping" program, for use with Hobbit.   */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitping.c,v 1.12 2006-07-20 16:06:41 henrik Exp $";

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
#include <arpa/inet.h>
#include <netdb.h>

#include <stdio.h>

#include "libbbgen.h"

#define PING_PACKET_SIZE 64
#define PING_MINIMUM_SIZE ICMP_MINLEN
#define SENDLIMIT 100

typedef struct hostdata_t {
	int id;
	struct sockaddr_in addr;	/* Address of host to ping */
	int received;			/* how many ICMP_ECHO replies we've got */
	struct timeval rtt_total;
	struct hostdata_t *next;
} hostdata_t;

hostdata_t *hosthead = NULL;
int hostcount = 0;
hostdata_t **hosts = NULL;	/* Array of pointers to the hostdata records, for fast acces via ID */
int myicmpid;
int senddelay = (1000000 / 50);	/* Delay between sending packets, in microseconds */

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
		/* Check if there are any commandline IP's */
		struct sockaddr_in ina;

		for (argi=1; ((argi < argc) && (inet_aton(argv[argi], &ina.sin_addr) == 0)); argi++) ;
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

		if (strlen(l) == 0) continue;

		newitem = (hostdata_t *)calloc(1, sizeof(hostdata_t));

		newitem->addr.sin_family = AF_INET;
		newitem->addr.sin_port = 0;
		if (inet_aton(l, &newitem->addr.sin_addr) == 0) {
			errprintf("Dropping %s - not an IP\n", l);
			free(newitem); 
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
	struct timeval timesent;	/* time we sent this ping */
} pingdata_t;


int send_ping(int sock, int startidx, int minresponses)
{
	static unsigned char buffer[PING_PACKET_SIZE];
	struct icmp *icmphdr;
	struct pingdata_t *pingdata;
	struct timezone tz;
	int sentbytes;
	int idx = startidx;

	/*
	 * Sends one ICMP "echo-request" packet.
	 *
	 * Note: A first attempt at this kept sending packets until
	 * we got an EWOULDBLOCK or a send error. This causes incoming
	 * responses to be dropped.
	 */

	/* Skip the hosts that have already delivered a ping response */
	while ((idx < hostcount) && (hosts[idx]->received >= minresponses)) idx++;
	if (idx >= hostcount) return hostcount;

	/* 
	 * Dont flood the net.
	 * By enforcing a brief sleep here, we force a delay
	 * between sending packets. It is easiest to do before sending
	 * a packet, because if done after the send completes, then
	 * it affects the RTT measurements.
	 */
	if (senddelay) usleep(senddelay);

	/* Build the packet and send it */
	memset(buffer, 0, PING_PACKET_SIZE);

	icmphdr = (struct icmp *)buffer;
	icmphdr->icmp_type = ICMP_ECHO;
	icmphdr->icmp_code = 0;
	icmphdr->icmp_cksum = 0;
	icmphdr->icmp_seq = htons(idx+1);	/* So we can map response to our hosts */
	icmphdr->icmp_id = htons(myicmpid);

	pingdata = (struct pingdata_t *)(buffer + sizeof(struct icmp));
	pingdata->id = idx;
	gettimeofday(&pingdata->timesent, &tz);

	icmphdr->icmp_cksum = calc_icmp_checksum((unsigned short *)buffer, PING_PACKET_SIZE);

	sentbytes = sendto(sock, buffer, PING_PACKET_SIZE, 0, 
			   (struct sockaddr *) &hosts[idx]->addr, sizeof(struct sockaddr_in));

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
				inet_ntoa(hosts[idx]->addr.sin_addr), idx, myicmpid);
		}
		idx++;
	}

	return idx;
}


int get_response(int sock)
{
	static unsigned char buffer[4096];
	struct sockaddr_in addr;
	int n, pktcount;
	unsigned int addrlen;
	struct ip *iphdr;
	int iphdrlen;
	struct icmp *icmphdr;
	struct pingdata_t *pingdata;
	int hostidx;
	struct timeval rtt;
	struct timezone tz;

	/*
	 * Read responses from the network.
	 * We know (because select() told us) that there is some data
	 * to read. To avoid losing packets, read as much as we can.
	 */
	pktcount = 0;
	do {
		addrlen = sizeof(addr);
		gettimeofday(&rtt, &tz);
		n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &addrlen);
		if (n < 0) {
			if (errno != EWOULDBLOCK) errprintf("Failed to receive packet: %s\n", strerror(errno));
			continue;
		}

		/* Check the IP header - we need to have at least enough bytes for an ICMP header. */
		iphdr = (struct ip *)buffer;
		iphdrlen = (iphdr->ip_hl << 2);	/* IP header always aligned on 4-byte boundary */
		if (n < (iphdrlen + PING_MINIMUM_SIZE)) {
			errprintf("Short packet ignored\n");
			continue;
		}

		/*
		 * Get the ICMP header, and our host index which is the sequence number.
		 * Thanks to "fping" for this neat way of matching requests and responses.
		 */
		icmphdr = (struct icmp *)(buffer + iphdrlen);
		hostidx = ntohs(icmphdr->icmp_seq)-1;

		if (debug) {
			dbgprintf("Got packet from %s: type=%d, index=%d, id=%d\n",
				inet_ntoa(addr.sin_addr), icmphdr->icmp_type, icmphdr->icmp_id, hostidx);
		}

		switch (icmphdr->icmp_type) {
		  case ICMP_ECHOREPLY:
			if (ntohs(icmphdr->icmp_id) != myicmpid) {
				/* Not one of our packets. Happens if someone else does ping simultaneously. */
				break;
			}

			if ((hostidx >= 0) && (hostidx < hostcount)) {
				/* Looks like one of our packets succeeded. */
				pktcount++;
				hosts[hostidx]->received += 1;

				pingdata = (struct pingdata_t *)(buffer + iphdrlen + sizeof(struct icmp));

				/* Calculate the round-trip time. */
				rtt.tv_sec -= pingdata->timesent.tv_sec;
				rtt.tv_usec -= pingdata->timesent.tv_usec;
				if (rtt.tv_usec < 0) {
					rtt.tv_sec--;
					rtt.tv_usec += 1000000;
				}

				/* Add RTT to the total time */
				hosts[hostidx]->rtt_total.tv_sec += rtt.tv_sec;
				hosts[hostidx]->rtt_total.tv_usec += rtt.tv_usec;
				if (hosts[hostidx]->rtt_total.tv_usec >= 1000000) {
					hosts[hostidx]->rtt_total.tv_sec++;
					hosts[hostidx]->rtt_total.tv_usec -= 1000000;
				}
			}
			break;

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
			errprintf("Got a packet that wasnt a reply - type %d\n", icmphdr->icmp_type);
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
	 * it directly in Hobbit without changing the bbtest-net code.
	 */
	for (idx = 0; (idx < hostcount); idx++) {
		if (hosts[idx]->received > 0) {
			printf("%s is alive", inet_ntoa(hosts[idx]->addr.sin_addr));
			rtt_usecs = (hosts[idx]->rtt_total.tv_sec*1000000 + hosts[idx]->rtt_total.tv_usec) / hosts[idx]->received;
			if (rtt_usecs >= 1000) {
				printf(" (%lu ms)\n", rtt_usecs / 1000);
			}
			else {
				printf(" (0.%02lu ms)\n", (rtt_usecs / 10));
			}
		}
		else {
			printf("%s is unreachable\n", inet_ntoa(hosts[idx]->addr.sin_addr));
		}
	}
}

int main(int argc, char *argv[])
{
	struct protoent *proto;
	int protonumber, pingsocket = -1, sockerr = 0, binderr = 0;
	int argi, sendidx, pending, minresponses = 1, tries = 3, timeout = 5;
	char *srcip = NULL;
	struct sockaddr_in src_addr;

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
			if (delim) set_debugfile(delim+1);
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			if (pingsocket >= 0) close(pingsocket);
			fprintf(stderr, "%s [--retries=N] [--timeout=N] [--responses=N] [--max-pps=N] [--source=IP]\n", argv[0]);
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

	proto = getprotobyname("icmp");
	protonumber = (proto ? proto->p_proto : 1);
	if (srcip != NULL) {
		/* Setup the source address */
		src_addr.sin_family = AF_INET;
		src_addr.sin_addr.s_addr = inet_addr(srcip);
		src_addr.sin_port = htons(0);
	}

	/* Get a raw socket. Requires root privs. */
	get_root();
	pingsocket = socket(AF_INET, SOCK_RAW, protonumber); sockerr = errno;
	if ((pingsocket != -1) && (srcip != NULL)) {
		/* Bind to a specific source address */
		if (bind(pingsocket, (struct sockaddr *) &src_addr, sizeof(src_addr)) == -1) binderr = errno;
	}
	drop_root();

	if (pingsocket == -1) {
		errprintf("Cannot get RAW socket: %s\n", strerror(sockerr));
		if (sockerr == EPERM) errprintf("This program must be installed suid-root\n");
		return 3;
	}

	if ((srcip != NULL) && (binderr != 0)) {
		errprintf("Cannot bind to source address %s: %s\nUsing default address\n",
			  srcip, strerror(binderr));
	}


	/* Set the socket non-blocking - we use select() exclusively */
	fcntl(pingsocket, F_SETFL, O_NONBLOCK);

	load_ips(argc, argv, stdin);
	pending = count_pending(minresponses);

	while (tries) {
		int sendnow = SENDLIMIT;
		time_t cutoff = time(NULL) + timeout + 1;
		sendidx = 0;

		/* Change this on each iteration, so we dont mix packets from each round of pings */
		myicmpid = ((getpid()+tries) & 0x7FFF);

		/* Do one loop over the hosts we havent had responses from yet. */
		while (pending > 0) {
			fd_set readfds, writefds;
			struct timeval tmo;
			int n;

			FD_ZERO(&readfds);
			FD_ZERO(&writefds);
			FD_SET(pingsocket, &readfds);

			if (sendnow && (sendidx < hostcount)) FD_SET(pingsocket, &writefds);

			tmo.tv_sec = 0;
			tmo.tv_usec = 100000;
			n = select(pingsocket+1, &readfds, &writefds, NULL, &tmo);

			if (n < 0) {
				if (errno == EINTR) continue;
				errprintf("select failed: %s\n", strerror(errno));
				return 4;
			}
			else if (n == 0) {
				/* Time out */
				if ((time(NULL) >= cutoff) && (sendidx >= hostcount)) {
					/* No more to send and the read timed out - so we're done */
					pending = 0;
				}
				sendnow = SENDLIMIT;
			}
			else {
				if (sendnow && FD_ISSET(pingsocket, &writefds)) {
					/* OK to send */
					sendidx = send_ping(pingsocket, sendidx, minresponses);
					sendnow--;

					/* Adjust the cutoff time, so we wait TIMEOUT seconds for a response */
					cutoff = time(NULL) + timeout + 1;
				}

				if (FD_ISSET(pingsocket, &readfds)) {
					/* Grab the replies */
					int count = get_response(pingsocket);
					pending -= count;
					sendnow += count; if (sendnow > SENDLIMIT) sendnow = SENDLIMIT;
				}
			}
		}

		tries--;
		pending = count_pending(minresponses);
		if (pending == 0) {
			/* Have got responses for all hosts - we're done */
			tries = 0;
		}
	}

	close(pingsocket);

	show_results();

	return 0;
}

