/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns.c,v 1.1 2004-08-18 21:53:54 henrik Exp $";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbtest-net.h"
#include "dns.h"

#ifdef ARES
#include <ares.h>
static ares_channel channel;
#else
#define ARES_FAILURE 0
#define ARES_SUCCESS 1
#endif

static int channelactive = 0;


int dns_stats_total   = 0;
int dns_stats_success = 0;
int dns_stats_failed  = 0;
int dns_stats_lookups = 0;

typedef struct dnsitem_t {
	char *name;
	struct in_addr addr;
	struct dnsitem_t *next;
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

	if (status == ARES_SUCCESS) {
		memcpy(&dnsc->addr, *(hent->h_addr_list), sizeof(dnsc->addr));
		dprintf("Got DNS result for host %s : %s\n", dnsc->name, inet_ntoa(dnsc->addr));
		dns_stats_success++;
	}
	else {
		memset(&dnsc->addr, 0, sizeof(dnsc->addr));
		dprintf("DNS lookup failed for %s - status %d\n", dnsc->name, status);
		dns_stats_failed++;
	}
}

void add_host_to_dns_queue(char *hostname)
{

	if (find_dnscache(hostname) == NULL) {
		/* New hostname */
		dnsitem_t *dnsc = (dnsitem_t *)calloc(1, sizeof(dnsitem_t));

		dprintf("Adding hostname '%s' to resolver queue\n", hostname);

		if (!channelactive) {
#ifdef ARES
			int status;
			status = ares_init(&channel);
			channelactive = 1;
#endif
		}

		dnsc->name = malcop(hostname);
		dnsc->next = dnscache;
		dnscache = dnsc;

#ifdef ARES
		ares_gethostbyname(channel, hostname, AF_INET, dns_callback, dnsc);
#else
		{
			struct hostent *hent = gethostbyname(hostname);
			dns_callback(dnsc, (hent ? ARES_SUCCESS : ARES_FAILURE), hent);
		}
#endif
		dns_stats_total++;
	}
}

void add_url_to_dns_queue(char *url)
{
	char *tempurl;
	char *fragment = NULL;
	char *scheme = NULL;
	char *auth = NULL;
	char *port = NULL;
	char *netloc;
	char *startp, *p;

	tempurl = malcop(realurl(url, NULL, NULL, NULL, NULL));
	fragment = strchr(tempurl, '#'); if (fragment) *fragment = '\0';

	/* First, skip the "scheme" (protocol) */
	startp = tempurl;
	p = strchr(startp, ':');
	if (p) {
		scheme = startp;
		*p = '\0';
		startp = (p+1);
	}

	if (strncmp(startp, "//", 2) == 0) {
		startp += 2;
		netloc = startp;

		p = strchr(startp, '/');
		if (p) {
			*p = '\0';
			startp = (p+1);
		}
		else startp += strlen(startp);
	}
	else {
		netloc = "";
	}

	/* netloc is [username:password@]hostname[:port] */
	auth = NULL;
	p = strchr(netloc, '@');
	if (p) {
		auth = netloc;
		*p = '\0';
		netloc = (p+1);
	}
	p = strchr(netloc, ':');
	if (p) {
		*p = '\0';
		port = (p+1);
	}

	if (strlen(netloc) > 0) add_host_to_dns_queue(netloc); 
	free(tempurl);
}


static void dns_queue_run(void)
{
#ifdef ARES
	int status, nfds;
	fd_set read_fds, write_fds;
	struct timeval *tvp, tv;

	while (1) {
		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);
		nfds = ares_fds(channel, &read_fds, &write_fds);
		if (nfds == 0)
			break;

		tv.tv_sec = 30; tv.tv_usec = 0;
		tvp = ares_timeout(channel, &tv, &tv);
		select(nfds, &read_fds, &write_fds, NULL, tvp);
		ares_process(channel, &read_fds, &write_fds);
	}

	ares_destroy(channel);
	channelactive = 0;
#endif
}

char *dnsresolve(char *hostname)
{
	char *result;

	dns_stats_lookups++;
	if (channelactive) dns_queue_run();

	result = find_dnscache(hostname);
	if (strcmp(result, "0.0.0.0") == 0) return NULL;

	return result;
}

