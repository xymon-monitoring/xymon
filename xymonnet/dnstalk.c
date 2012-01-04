/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns2.c 6743 2011-09-03 15:44:52Z storner $";

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "ares.h"

#include "libxymon.h"

#include "tcptalk.h"
#include "dnstalk.h"
#include "dnsbits.h"

static int dns_atype, dns_aaaatype, dns_aclass;
static void dns_lookup_init(void);
static ares_channel dns_lookupchannel;

void dns_library_init(void)
{
	int status;

	status = ares_library_init(ARES_LIB_INIT_ALL);
	if (status != ARES_SUCCESS) {
		errprintf("Cannot initialize ARES library: %s\n", ares_strerror(status));
		return;
	}
	
	dns_atype= dns_name_type("A");
	dns_aaaatype= dns_name_type("AAAA");
	dns_aclass = dns_name_class("IN");

	dns_lookup_init();
}

static void destroy_addr_list(struct ares_addr_node *head)
{
	while(head)
	{
		struct ares_addr_node *detached = head;
		head = head->next;
		free(detached);
	}
}

static void append_addr_list(struct ares_addr_node **head, struct ares_addr_node *node)
{
	struct ares_addr_node *last;
	node->next = NULL;
	if(*head)
	{
		last = *head;
		while(last->next)
			last = last->next;
		last->next = node;
	}
	else
		*head = node;
}


int dns_start_query(myconn_t *rec, char *targetserver)
{
	struct ares_addr_node *srvr, *servers = NULL;
	struct ares_options options;
	int status, optmask;
	char *tdup, *tst;

	/* See what IP family we must use to communicate with the target DNS server */
	srvr = malloc(sizeof(struct ares_addr_node));
	append_addr_list(&servers, srvr);
	srvr->family = -1;
#ifdef IPV4_SUPPORT
	if ((srvr->family == -1) && (inet_pton(AF_INET, targetserver, &srvr->addr.addr4) > 0))
		srvr->family = AF_INET;
#endif

#ifdef IPV6_SUPPORT
	if ((srvr->family == -1) && (inet_pton(AF_INET6, targetserver, &srvr->addr.addr6) > 0))
		srvr->family = AF_INET6;
#endif

	if (srvr->family == -1) {
		errprintf("Unsupported IP family for DNS target IP %s, test %s\n", targetserver, rec->testspec);
		return 0;
	}

	/* Create a new ARES request channel */
	rec->dnschannel = malloc(sizeof(ares_channel));

	/* 
	 * The C-ARES timeout handling is a bit complicated. The timeout setting
	 * here in the options only determines the timeout for the first query;
	 * subsequent queries (up to the "tries" count) use a progressively
	 * higher timeout setting.
	 * So we cannot easily determine what combination of timeout/tries will
	 * result in the full query timing out after the desired number of seconds.
	 * Therefore, use a fixed set of values - the 2000 ms / 4 tries combination
	 * results in a timeout after 23-24 seconds.
	 */
	optmask = ARES_OPT_FLAGS | ARES_OPT_SERVERS | ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES;
	options.flags = ARES_FLAG_NOCHECKRESP;
	options.servers = NULL;
	options.nservers = 0;
	options.timeout = 2000;
	options.tries = 4;
	status = ares_init_options(rec->dnschannel, &options, optmask);
	if (status != ARES_SUCCESS) {
		errprintf("Cannot create ARES channel for DNS target %s, test %s\n", targetserver, rec->testspec);
		rec->dnsstatus = DNS_FINISHED;
		return 0;
	}

	/* Point the channel at the target server */
	status = ares_set_servers(*((ares_channel *)rec->dnschannel), servers);
	destroy_addr_list(servers);
	if (status != ARES_SUCCESS)
	{
		errprintf("Cannot select ARES target DNS server %s, test %s\n", targetserver, rec->testspec);
		rec->dnsstatus = DNS_QUERY_COMPLETED;	/* To reap the ARES channel later */
		return 0;
	}

	/* Post the queries we want to perform */
	tdup = strdup(rec->testspec);

	tst = strtok(tdup, ",");
	do {
		char *p, *tlookup;
		int atype = dns_atype;

		p = strchr(tst, ':');
		tlookup = (p ? p+1 : tst);
		if (p) { 
			*p = '\0'; 
			atype = dns_name_type(tst);
			*p = ':';
		}

		/* Use ares_query() here, since we dont want to get results from hosts file or other odd stuff. */
		ares_query(*((ares_channel *)rec->dnschannel), tlookup, dns_aclass, atype, dns_query_callback, rec);
		tst = strtok(NULL, ",");
	} while (tst);

	xfree(tdup);

	rec->textlog = newstrbuffer(0);
	getntimer(&rec->dnsstarttime);
	rec->dnsstatus = DNS_QUERY_ACTIVE;

	return 1;
}


/* TALK_PROTO_PLAIN, TALK_PROTO_NTP, TALK_PROTO_HTTP, TALK_PROTO_DNSQUERY, TALK_PROTO_DNSLOOKUP */
char *prototxt[] = { "plain", "ntp", "http", "dnsquery", "dnslookup" };
/* DNS_NOTDONE, DNS_QUERY_READY, DNS_QUERY_ACTIVE, DNS_QUERY_COMPLETED, DNS_FINISHED */
char *dnsstatustxt[] = { "notdone", "ready", "active", "completed", "finished" };

int dns_add_active_fds(listhead_t *activelist, int *maxfd, fd_set *fdread, fd_set *fdwrite)
{
	listitem_t *walk;
	int n, activecount = 0;

	// dbgprintf("DNS - check active fds\n");
	for (walk = activelist->head; (walk); walk = walk->next) {
		myconn_t *rec = (myconn_t *)walk->data;

		//dbgprintf("\t%s - proto %s, dnsstatus %s\n", rec->testspec, prototxt[rec->talkprotocol], dnsstatustxt[rec->dnsstatus]);
		if (rec->talkprotocol != TALK_PROTO_DNSQUERY) continue;
		if (rec->dnsstatus != DNS_QUERY_ACTIVE) continue;

		activecount++;

		//dbgprintf("DNS query %s has an active fd\n", rec->testspec);

		/* 
		 * ares_fds() returns the max FD number +1, i.e. the value you would use for select()
		 * But we expect maxfd to be just the max FD number. So use "(n-1)" instead of the
		 * value directly from ares_fds()
		 */
		n = ares_fds(*((ares_channel *)rec->dnschannel), fdread, fdwrite);
		if ((n-1) > *maxfd) *maxfd = (n-1);
	}

	/* ... and the lookup channel ... */
	n = ares_fds(dns_lookupchannel, fdread, fdwrite);
	if (n != 0) activecount++;
	if ((n-1) > *maxfd) *maxfd = (n-1);

	return activecount;
}


void dns_process_active(listhead_t *activelist, fd_set *fdread, fd_set *fdwrite)
{
	listitem_t *walk;

	// dbgprintf("DNS - process active fds\n");
	for (walk = activelist->head; (walk); walk = walk->next) {
		myconn_t *rec = (myconn_t *)walk->data;

		// dbgprintf("\t%s - proto %s, dnsstatus %s\n", rec->testspec, prototxt[rec->talkprotocol], dnsstatustxt[rec->dnsstatus]);
		if (rec->talkprotocol != TALK_PROTO_DNSQUERY) continue;
		if (rec->dnsstatus != DNS_QUERY_ACTIVE) continue;

		// dbgprintf("DNS query %s processing\n", rec->testspec);
		ares_process(*((ares_channel *)rec->dnschannel), fdread, fdwrite);
	}

	/* ... and the lookup channel ... */
	ares_process(dns_lookupchannel, fdread, fdwrite);
}


void dns_finish_queries(listhead_t *activelist)
{
	listitem_t *walk;

	for (walk = activelist->head; (walk); walk = walk->next) {
		myconn_t *rec = (myconn_t *)walk->data;
		if (rec->talkprotocol != TALK_PROTO_DNSQUERY) continue;
		if (rec->dnsstatus != DNS_QUERY_COMPLETED) continue;

		dbgprintf("DNS query %s cleanup\n", rec->testspec);
		ares_destroy(*((ares_channel *)rec->dnschannel));
		rec->dnsstatus = DNS_FINISHED;
	}

	/* Nothing to do for the lookup channel */
}



static void dns_lookup_init(void)
{
	struct ares_options options;
	int optmask, status;

	optmask = ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES | ARES_OPT_FLAGS;
	options.flags = ARES_FLAG_STAYOPEN;
	options.timeout = 2000;
	options.tries = 4;
	status = ares_init_options(&dns_lookupchannel, &options, optmask);
	if (status != ARES_SUCCESS) {
		errprintf("Cannot initialise DNS lookups: %s\n", ares_strerror(status));
		return;
	}
}

/* This defines the sequence in which we perform DNS lookup for IPv4 and IPv6 - default is IPv4 first, then v6 */
static const int dns_lookup_sequence[] = {
#ifdef IPV4_SUPPORT
	AF_INET,
#endif
#ifdef IPV6_SUPPORT
	AF_INET6,
#endif
	-1
};

void dns_lookup(myconn_t *rec)
{
	/* Push a normal DNS lookup into the DNS queue */

	if (rec->netparams.af_index == 0) getntimer(&rec->netparams.lookupstart);

	/* Use ares_search() here, we want to use the whole shebang of name lookup options */
	if (dns_lookup_sequence[rec->netparams.af_index] != -1) {
		ares_gethostbyname(dns_lookupchannel, rec->netparams.lookupstring, dns_lookup_sequence[rec->netparams.af_index], dns_lookup_callback, rec);
		rec->netparams.lookupstatus = LOOKUP_ACTIVE;
		rec->netparams.af_index++;
	}
	else {
		rec->netparams.lookupstatus = LOOKUP_FAILED;
		rec->talkresult = TALK_CANNOT_RESOLVE;
		test_is_done(rec);
	}
}

