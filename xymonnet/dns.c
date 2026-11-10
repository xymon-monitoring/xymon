/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <sys/time.h>

#include "libxymon.h"

#include <ares.h>
#include <ares_version.h>

#include "dns.h"
#include "dns2.h"

static ares_channel mychannel;
static int pending_dns_count = 0;
int use_ares_lookup = 1;
int max_dns_per_run = 0;

int dns_stats_total   = 0;
int dns_stats_success = 0;
int dns_stats_failed  = 0;
int dns_stats_lookups = 0;
int dnstimeout        = 30;

FILE *dnsfaillog = NULL;


typedef struct dnsitem_t {
	char *name;
	char addr[IP_ADDR_STRLEN];
	struct dnsitem_t *next;
	int failed;
	int triedv6;
	struct timespec resolvetime;
} dnsitem_t;

static void * dnscache;

static void dns_init(void)
{
	static int initdone = 0;

	if (initdone) return;

	dnscache = xtreeNew(strcasecmp);

	if (use_ares_lookup) {
		struct ares_options options;
		int status;

		/* ARES timeout backported from Xymon trunk 20120411 - this should give us a ~23 second timeout */
		options.timeout = 2000;
		options.tries = 4;

		status = ares_init_options(&mychannel, &options, (ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES));
		if (status != ARES_SUCCESS) {
			errprintf("Cannot initialize ARES resolver, using standard\n");
			errprintf("ARES error was: '%s'\n", ares_strerror(status));
			use_ares_lookup = 0;
		}
	}

	initdone = 1;
}

static char *find_dnscache(char *hostname)
{
	struct in_addr in4;
	unsigned char in6[16];
	xtreePos_t handle;
	dnsitem_t *dnsc;

	dns_init();

	/* inet_aton() first: it reads the IPv4 short forms, such as 127.1, as before */
	if ((inet_aton(hostname, &in4) != 0) || (inet_pton(AF_INET6, hostname, in6) == 1)) {
		/* It is an IP, so just use that */
		return hostname;
	}

	/* In the cache ? */
	handle = xtreeFind(dnscache, hostname);
	if (handle == xtreeEnd(dnscache)) return NULL;

	dnsc = (dnsitem_t *)xtreeData(dnscache, handle);
	return dnsc->addr;
}


/* dns_done -- record a lookup's address, or with ip NULL its failure */
static void dns_done(dnsitem_t *dnsc, char *ip, const char *errtext, int err)
{
	struct timespec etime;

	getntimer(&etime);
	tvdiff(&dnsc->resolvetime, &etime, &dnsc->resolvetime);
	pending_dns_count--;

	if (ip) {
		snprintf(dnsc->addr, sizeof(dnsc->addr), "%s", ip);
		dbgprintf("Got DNS result for host %s : %s\n", dnsc->name, dnsc->addr);
		dns_stats_success++;
	}
	else {
		strcpy(dnsc->addr, "0.0.0.0");
		dbgprintf("DNS lookup failed for %s - status %s (%d)\n", dnsc->name, errtext, err);
		dnsc->failed = 1;
		dns_stats_failed++;

		if (dnsfaillog) {
			fprintf(dnsfaillog, "DNS lookup failed for %s - status %s (%d)\n", 
				dnsc->name, errtext, err);
		}
	}
}

static void dns_simple_callback(void *arg, int status, int timeout, struct hostent *hent)
{
	struct dnsitem_t *dnsc = (dnsitem_t *)arg;
	char ip[IP_ADDR_STRLEN];

	/*
	 * No IPv4 address: an IPv6 one, before giving up. Not after a timeout,
	 * which the second lookup would only repeat.
	 */
	if ((status != ARES_SUCCESS) && (status != ARES_ETIMEOUT) && (status != ARES_ECANCELLED) &&
	    (status != ARES_EDESTRUCTION) && !dnsc->triedv6) {
		dnsc->triedv6 = 1;
		ares_gethostbyname(mychannel, dnsc->name, AF_INET6, dns_simple_callback, dnsc);
		return;
	}

	if ((status == ARES_SUCCESS) && inet_ntop(hent->h_addrtype, hent->h_addr_list[0], ip, sizeof(ip)))
		dns_done(dnsc, ip, NULL, 0);
	else
		dns_done(dnsc, NULL, ares_strerror(status), status);
}



static void dns_ares_queue_run(ares_channel channel)
{
	int nfds;
	fd_set read_fds, write_fds;
	struct timeval *tvp, tv;
	int loops = 0;

	if ((channel == mychannel) && (!pending_dns_count)) return;

	dbgprintf("Processing %d DNS lookups with ARES\n", pending_dns_count);

	while (1) {	/* Loop continues until all requests handled (or time out) */
		loops++;
		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);
		nfds = ares_fds(channel, &read_fds, &write_fds);
		if (nfds == 0) {
			dbgprintf("Finished ARES queue after loop %d\n", loops);
			break;	/* No pending requests */
		}

		/* 
		 * Determine how long select() waits before processing timeouts.
		 * "dnstimeout" is the user configurable option which is
		 * the absolute maximum timeout value. However, ARES also
		 * has built in timeouts - these are defined at ares_init()
		 * using ARES_OPT_TIMEOUTMS and ARES_OPT_TRIES (default
		 * 5 secs / 4 tries = 20 second timeout).
		 */
		tv.tv_sec = dnstimeout; tv.tv_usec = 0;
		tvp = ares_timeout(channel, &tv, &tv);

		select(nfds, &read_fds, &write_fds, NULL, tvp);
		ares_process(channel, &read_fds, &write_fds);
	}

	if (pending_dns_count > 0) {
		errprintf("Odd ... pending_dns_count=%d after a queue run\n", 
				pending_dns_count);
		pending_dns_count = 0;
	}
}


void add_host_to_dns_queue(char *hostname)
{
	dnsitem_t *dnsc;

	dns_init();
	dns_stats_total++;

	if (find_dnscache(hostname)) return; 	/* Already resolved */

	if (max_dns_per_run && (pending_dns_count >= max_dns_per_run)) {
		/* Limit the number of requests we do per run */
		dns_ares_queue_run(mychannel);
	}

	/* New hostname */
	dnsc = (dnsitem_t *)calloc(1, sizeof(dnsitem_t));

	dbgprintf("Adding hostname '%s' to resolver queue\n", hostname);
	pending_dns_count++;

	dnsc->name = strdup(hostname);
	getntimer(&dnsc->resolvetime);
	xtreeAdd(dnscache, dnsc->name, dnsc);

	if (use_ares_lookup) {
		ares_gethostbyname(mychannel, hostname, AF_INET, dns_simple_callback, dnsc);
	}
	else {
		/* The system resolver, through the same bookkeeping as ARES */
		char ip[IP_ADDR_STRLEN];
		int err;

		if (host_address(hostname, ip, sizeof(ip), &err))
			dns_done(dnsc, ip, NULL, 0);
		else
			dns_done(dnsc, NULL, gai_strerror(err), err);
	}
}

void add_url_to_dns_queue(char *url)
{
	weburl_t weburl;

	dns_init();

	decode_url(url, &weburl);

	if (weburl.proxyurl) {
		if (weburl.proxyurl->parseerror) return;
		add_host_to_dns_queue(weburl.proxyurl->host); 
	}
	else {
		if (weburl.desturl->parseerror) return;
		add_host_to_dns_queue(weburl.desturl->host); 
	}
}


void flush_dnsqueue(void)
{
	dns_init();
	dns_ares_queue_run(mychannel);
}

char *dnsresolve(char *hostname)
{
	char *result;

	dns_init();

	if (hostname == NULL) return NULL;

	flush_dnsqueue();
	dns_stats_lookups++;

	result = find_dnscache(hostname);
	if (result == NULL) {
		errprintf("dnsresolve: name '%s' not in cache. You probably have a NULL IP setting and the 'testip' flag set.\n", hostname);
		return NULL;
	}
	if (strcmp(result, "0.0.0.0") == 0) return NULL;

	return result;
}

int dns_test_server(char *serverip, char *hostname, strbuffer_t *banner)
{
	ares_channel channel;
	struct ares_options options;
	struct in_addr serveraddr;
	int status;
	struct timespec starttime, endtime;
	struct timespec *tspent;
	char msg[100];
	SBUF_DEFINE(tspec);
	char *tst;
	dns_resp_t *responses = NULL;
	dns_resp_t *walk = NULL;
	int i;

	dns_init();

	if (inet_aton(serverip, &serveraddr) == 0) {
		errprintf("dns_test_server: serverip '%s' not a valid IP\n", serverip);
		return 1;
	}

	options.flags = ARES_FLAG_NOCHECKRESP;
	options.servers = &serveraddr;
	options.nservers = 1;
	/* ARES timeout backported from Xymon trunk 20120411 - this should give us a ~23 second timeout */
	options.timeout = 2000;
	options.tries = 4;

	status = ares_init_options(&channel, &options, (ARES_OPT_FLAGS | ARES_OPT_SERVERS | ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES));
	if (status != ARES_SUCCESS) {
		errprintf("Could not initialize ares channel: %s\n", ares_strerror(status));
		return 1;
	}

	tspec = strdup(hostname);
	tspec_buflen = strlen(tspec) + 1;
	getntimer(&starttime);
	tst = strtok(tspec, ",");
	do {
		dns_resp_t *newtest = (dns_resp_t *)malloc(sizeof(dns_resp_t));
		char *p, *tlookup;
		int atype = T_A;

		newtest->msgbuf = newstrbuffer(0);
		newtest->next = NULL;
		if (responses == NULL) responses = newtest; else walk->next = newtest;
		walk = newtest;

		p = strchr(tst, ':');
		tlookup = (p ? p+1 : tst);
		if (p) { *p = '\0'; atype = dns_name_type(tst); *p = ':'; }

		dbgprintf("ares_search: tlookup='%s', class=%d, type=%d\n", tlookup, C_IN, atype);
		ares_search(channel, tlookup, C_IN, atype, dns_detail_callback, newtest);
		tst = strtok(NULL, ",");
	} while (tst);

	dns_ares_queue_run(channel);

	getntimer(&endtime);
	tspent = tvdiff(&starttime, &endtime, NULL);
	clearstrbuffer(banner); status = ARES_SUCCESS;
	strncpy(tspec, hostname, tspec_buflen);
	tst = strtok(tspec, ",");
	for (walk = responses, i=1; (walk); walk = walk->next, i++) {
		/* Print an identifying line if more than one query */
		if ((walk != responses) || (walk->next)) {
			snprintf(msg, sizeof(msg), "\n*** DNS lookup of '%s' ***\n", tst);
			addtobuffer(banner, msg);
		}
		addtostrbuffer(banner, walk->msgbuf);
		if (walk->msgstatus != ARES_SUCCESS) status = walk->msgstatus;
		xfree(walk->msgbuf);
		tst = strtok(NULL, ",");
	}
	xfree(tspec);
	snprintf(msg, sizeof(msg), "\nSeconds: %u.%.9ld\n", (unsigned int)tspent->tv_sec, tspent->tv_nsec);
	addtobuffer(banner, msg);

	ares_destroy(channel);

	return (status != ARES_SUCCESS);
}

