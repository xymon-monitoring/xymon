/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns.c,v 1.32 2008-01-03 09:42:11 henrik Exp $";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <sys/time.h>

#include "libbbgen.h"

#include "dns.h"
#include "dns2.h"

#include <ares.h>

#ifdef HPUX
/* Doesn't have hstrerror */
char *hstrerror(int err) { return ""; }
#endif

static ares_channel stdchannel;
static int stdchannelactive = 0;
int use_ares_lookup = 1;

int dns_stats_total   = 0;
int dns_stats_success = 0;
int dns_stats_failed  = 0;
int dns_stats_lookups = 0;
int dnstimeout        = 30;

static int pending_dns_count = 0;
int max_dns_per_run = 500;
FILE *dnsfaillog = NULL;

static void dns_queue_run(ares_channel channel);

typedef struct dnsitem_t {
	char *name;
	struct in_addr addr;
	struct dnsitem_t *next;
	int failed;
	struct timeval resolvetime;
} dnsitem_t;

static RbtHandle dnscache;
static int dnscachepreped = 0;


static void prepare_dnscache(void)
{
	if (dnscachepreped) return;

	dnscache = rbtNew(name_compare);
	dnscachepreped = 1;
}

static char *find_dnscache(char *hostname)
{
	struct in_addr inp;
	RbtIterator handle;
	dnsitem_t *dnsc;

	if (!dnscachepreped) prepare_dnscache();

	if (inet_aton(hostname, &inp) != 0) {
		/* It is an IP, so just use that */
		return hostname;
	}

	/* In the cache ? */
	handle = rbtFind(dnscache, hostname);
	if (handle == rbtEnd(dnscache)) return NULL;

	dnsc = (dnsitem_t *)gettreeitem(dnscache, handle);
	return inet_ntoa(dnsc->addr);
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
		dbgprintf("Got DNS result for host %s : %s\n", dnsc->name, inet_ntoa(dnsc->addr));
		if (stdchannelactive) dns_stats_success++;
	}
	else {
		memset(&dnsc->addr, 0, sizeof(dnsc->addr));
		dbgprintf("DNS lookup failed for %s - status %s (%d)\n", dnsc->name, ares_strerror(status), status);
		dnsc->failed = 1;
		if (stdchannelactive) {
			if (dnsfaillog) {
				fprintf(dnsfaillog, "DNS lookup failed for %s - status %s (%d)\n", 
					dnsc->name, ares_strerror(status), status);
			}
			dns_stats_failed++;
		}
	}
}

void add_host_to_dns_queue(char *hostname)
{
	struct timezone tz;

	if (!dnscachepreped) prepare_dnscache();

	if (stdchannelactive && (pending_dns_count >= max_dns_per_run)) {
		dns_queue_run(stdchannel);
		pending_dns_count = 0;
	}

	if (find_dnscache(hostname) == NULL) {
		/* New hostname */
		dnsitem_t *dnsc = (dnsitem_t *)calloc(1, sizeof(dnsitem_t));

		dbgprintf("Adding hostname '%s' to resolver queue\n", hostname);
		pending_dns_count++;

		if (use_ares_lookup && !stdchannelactive) {
			int status;
			status = ares_init(&stdchannel);
			if (status == ARES_SUCCESS) {
				stdchannelactive = 1;
			}
			else {
				errprintf("Cannot initialize ARES resolver, using standard\n");
				errprintf("ARES error was: '%s'\n", ares_strerror(status));
				use_ares_lookup = 0;
			}
		}

		dnsc->name = strdup(hostname);
		gettimeofday(&dnsc->resolvetime, &tz);
		rbtInsert(dnscache, dnsc->name, dnsc);

		if (use_ares_lookup) {
			ares_gethostbyname(stdchannel, hostname, AF_INET, dns_callback, dnsc);
		}
		else {
			struct hostent *hent;
			int status;

			hent = gethostbyname(hostname);
			if (hent) {
				status = ARES_SUCCESS;
				dns_stats_success++;
			}
			else {
				status = ARES_ENOTFOUND;
				dns_stats_failed++;
				dbgprintf("gethostbyname() failed with err %d: %s\n", h_errno, hstrerror(h_errno));
				if (dnsfaillog) {
					fprintf(dnsfaillog, "Hostname lookup failed for %s - status %s (%d)\n", 
						hostname, hstrerror(h_errno), h_errno);
				}
			}
			dns_callback(dnsc, status, hent);
		}

		dns_stats_total++;
	}
}

void add_url_to_dns_queue(char *url)
{
	bburl_t bburl;

	if (!dnscachepreped) prepare_dnscache();

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
	int nfds, selres;
	fd_set read_fds, write_fds;
	struct timeval *tvp, tv;
	int progress = 10;
	int loops = 0;
	struct timeval cutoff, now;
	struct timezone tz;

	dbgprintf("Processing %d DNS lookups with ARES\n", pending_dns_count);
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
	if (stdchannelactive && (channel == stdchannel)) {
		stdchannelactive = 0;
	}
}

void flush_dnsqueue(void)
{
	if (!dnscachepreped) prepare_dnscache();

	if (stdchannelactive) {
		dns_queue_run(stdchannel);
		stdchannelactive = 0;
	}
}

char *dnsresolve(char *hostname)
{
	char *result;

	if (!dnscachepreped) prepare_dnscache();

	if (hostname == NULL) return NULL;

	flush_dnsqueue();
	dns_stats_lookups++;

	result = find_dnscache(hostname);
	if (result == NULL) {
		errprintf("dnsresolve - internal error, name '%s' not in cache\n", hostname);
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
	struct timeval starttime, endtime;
	struct timeval *tspent;
	struct timezone tz;
	char msg[100];
	char *tspec, *tst;
	dns_resp_t *responses = NULL;
	dns_resp_t *walk = NULL;
	int i;

	if (!dnscachepreped) prepare_dnscache();

	if (inet_aton(serverip, &serveraddr) == 0) {
		errprintf("dns_test_server: serverip '%s' not a valid IP\n", serverip);
		return 1;
	}

	options.flags = ARES_FLAG_NOCHECKRESP;
	options.servers = &serveraddr;
	options.nservers = 1;

	status = ares_init_options(&channel, &options, (ARES_OPT_FLAGS | ARES_OPT_SERVERS));
	if (status != ARES_SUCCESS) {
		errprintf("Could not initialize ares channel: %s\n", ares_strerror(status));
		return 1;
	}

	tspec = strdup(hostname);
	gettimeofday(&starttime, &tz);
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

	dns_queue_run(channel);
	gettimeofday(&endtime, &tz);
	tspent = tvdiff(&starttime, &endtime, NULL);
	clearstrbuffer(banner); status = ARES_SUCCESS;
	strcpy(tspec, hostname);
	tst = strtok(tspec, ",");
	for (walk = responses, i=1; (walk); walk = walk->next, i++) {
		/* Print an identifying line if more than one query */
		if ((walk != responses) || (walk->next)) {
			sprintf(msg, "\n*** DNS lookup of '%s' ***\n", tst);
			addtobuffer(banner, msg);
		}
		addtostrbuffer(banner, walk->msgbuf);
		if (walk->msgstatus != ARES_SUCCESS) status = walk->msgstatus;
		xfree(walk->msgbuf);
		tst = strtok(NULL, ",");
	}
	xfree(tspec);
	sprintf(msg, "\nSeconds: %u.%03u\n", (unsigned int)tspent->tv_sec, (unsigned int)tspent->tv_usec/1000);
	addtobuffer(banner, msg);

	return (status != ARES_SUCCESS);
}

