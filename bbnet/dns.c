/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns.c,v 1.9 2004-08-31 20:38:21 henrik Exp $";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbtest-net.h"
#include "dns.h"
#include "dns2.h"

#include <ares.h>
static ares_channel stdchannel;
static int stdchannelactive = 0;

int dns_stats_total   = 0;
int dns_stats_success = 0;
int dns_stats_failed  = 0;
int dns_stats_lookups = 0;
int dnstimeout        = 30;

typedef struct dnsitem_t {
	char *name;
	struct in_addr addr;
	struct dnsitem_t *next;
	int failed;
	struct timeval resolvetime;
} dnsitem_t;

static dnsitem_t *dnscache = NULL;

static char *find_dnscache(char *hostname)
{
	struct in_addr inp;
	struct hostent *hent;
	dnsitem_t *dnsc;

	if (inet_aton(hostname, &inp) != 0) {
		/* It is an IP, so just use that */
		return hostname;
	}

	/* In the cache ? */
	for (dnsc = dnscache; (dnsc && (strcmp(dnsc->name, hostname) != 0)); dnsc = dnsc->next);
	if (dnsc) return inet_ntoa(dnsc->addr);

	/* No such name */
	return NULL;
}


static void dns_callback(void *arg, int status, struct hostent *hent)
{
	dnsitem_t *dnsc = (dnsitem_t *) arg;
	struct timeval etime;
	struct timezone tz;

	gettimeofday(&etime, &tz);
	tvdiff(&dnsc->resolvetime, &etime, &dnsc->resolvetime);

	if (status == ARES_SUCCESS) {
		memcpy(&dnsc->addr, *(hent->h_addr_list), sizeof(dnsc->addr));
		dprintf("Got DNS result for host %s : %s\n", dnsc->name, inet_ntoa(dnsc->addr));
		if (stdchannelactive) dns_stats_success++;
	}
	else {
		memset(&dnsc->addr, 0, sizeof(dnsc->addr));
		dprintf("DNS lookup failed for %s - status %d\n", dnsc->name, status);
		dnsc->failed = 1;
		if (stdchannelactive) dns_stats_failed++;
	}
}

void add_host_to_dns_queue(char *hostname)
{
	struct timezone tz;

	if (find_dnscache(hostname) == NULL) {
		/* New hostname */
		dnsitem_t *dnsc = (dnsitem_t *)calloc(1, sizeof(dnsitem_t));

		dprintf("Adding hostname '%s' to resolver queue\n", hostname);

		if (!stdchannelactive) {
			int status;
			status = ares_init(&stdchannel);
			stdchannelactive = 1;
		}

		dnsc->name = malcop(hostname);
		dnsc->failed = 0;
		dnsc->next = dnscache;
		gettimeofday(&dnsc->resolvetime, &tz);
		dnscache = dnsc;

		ares_gethostbyname(stdchannel, hostname, AF_INET, dns_callback, dnsc);
		dns_stats_total++;
	}
}

void add_url_to_dns_queue(char *url)
{
	bburl_t bburl;

	decode_url(url, &bburl);

	if (bburl.proxyurl) {
		if (bburl.proxyurl->parseerror) return;
		add_host_to_dns_queue(bburl.proxyurl->host); 
	}
	else {
		if (bburl.desturl->parseerror) return;
		add_host_to_dns_queue(bburl.desturl->host); 
	}
}


static void dns_queue_run(ares_channel channel)
{
	int status, nfds, selres;
	fd_set read_fds, write_fds;
	struct timeval *tvp, tv;
	int progress = 10;
	int loops = 0;
	struct timeval cutoff, now;
	struct timezone tz;

	gettimeofday(&cutoff, &tz);
	cutoff.tv_sec += dnstimeout + 1;

	do {
		loops++;
		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);
		nfds = ares_fds(channel, &read_fds, &write_fds);
		if (nfds == 0) break;

		tv.tv_sec = 10; tv.tv_usec = 0;
		tvp = ares_timeout(channel, &tv, &tv);
		selres = select(nfds, &read_fds, &write_fds, NULL, tvp);
		ares_process(channel, &read_fds, &write_fds);

		/* 
		 * This is a guesstimate way of preventing this from being an
		 * infinite loop. select must return with some sort of data; 
		 * if it does not, then a timeout happened and we'll tolerate
		 * those only for a limited number of times.
		 */
		if (selres > 0) { 
			progress = 10; 
		}
		else {
			progress--;
			if (!progress) {
				errprintf("dns_queue_run deadlock - loops=%d\n", loops);
			}
		}
		gettimeofday(&now, &tz);
	} while (progress && (now.tv_sec < cutoff.tv_sec));

	ares_destroy(channel);
}

char *dnsresolve(char *hostname)
{
	char *result;

	if (hostname == NULL) return NULL;

	dns_stats_lookups++;
	if (stdchannelactive) {
		dns_queue_run(stdchannel);
		stdchannelactive = 0;
	}

	result = find_dnscache(hostname);
	if (result == NULL) {
		errprintf("dnsresolve - internal error, name '%s' not in cache\n", hostname);
		return NULL;
	}
	if (strcmp(result, "0.0.0.0") == 0) return NULL;

	return result;
}

int dns_test_server(char *serverip, char *hostname, char **banner, int *bannerbytes)
{
	ares_channel channel;
	struct ares_options options;
	struct in_addr serveraddr;
	int status;
	struct timeval starttime, endtime;
	struct timeval *tspent;
	struct timezone tz;
	char msg[100];

	if (inet_aton(serverip, &serveraddr) == 0) {
		errprintf("dns_test_server: serverip '%s' not a valid IP\n", serverip);
		return 1;
	}

	options.flags = ARES_FLAG_NOCHECKRESP;
	options.servers = &serveraddr;
	options.nservers = 1;

	status = ares_init_options(&channel, &options, (ARES_OPT_FLAGS | ARES_OPT_SERVERS));
	if (status != ARES_SUCCESS) {
		errprintf("Could not initialize ares channel %d\n", status);
		return 1;
	}

	gettimeofday(&starttime, &tz);
	ares_query(channel, hostname, C_IN, T_A, dns_detail_callback, NULL);
	dns_queue_run(channel);
	gettimeofday(&endtime, &tz);
	tspent = tvdiff(&starttime, &endtime, NULL);
	*banner = dns_detail_response(&status);
	*bannerbytes = strlen(*banner);
	sprintf(msg, "\nSeconds: %u.%03u\n", (unsigned int)tspent->tv_sec, (unsigned int)tspent->tv_usec/1000);
	addtobuffer(banner, bannerbytes, msg);

	return (status != ARES_SUCCESS);
}

