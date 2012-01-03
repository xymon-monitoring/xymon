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

#include "ares.h"

#include "libxymon.h"

#include "tcptalk.h"
#include "dnstalk.h"
#include "dnsbits.h"

static myconn_t *dnshead = NULL;

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

void dns_init_channel(myconn_t *rec)
{
	static int libstatus = -1;
	ares_channel *channel;

	if (libstatus == -1) {
		libstatus = ares_library_init(ARES_LIB_INIT_ALL);
		if (libstatus != ARES_SUCCESS) {
			rec->dnsstatus = DNS_FINISHED;
			return;
		}
	}

	channel = (ares_channel *)malloc(sizeof(ares_channel));
	rec->dnschannel = channel;
	rec->dnsnext = dnshead;
	dnshead = rec;
}


int dns_start_query(myconn_t *rec, char *targetserver)
{
	struct ares_addr_node *srvr, *servers = NULL;
	struct ares_options options;
	int status, optmask;
	char *tdup, *tst;

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
		errprintf("Unsupported DNS target IP %s\n", targetserver);
		return 0;
	}

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
		rec->dnsstatus = DNS_FINISHED;
		return 0;
	}

	status = ares_set_servers(*((ares_channel *)rec->dnschannel), servers);
	destroy_addr_list(servers);
	if (status != ARES_SUCCESS)
	{
		errprintf("ares_init_options failed: %s\n", ares_strerror(status));
		return 1;
	}


	tdup = strdup(rec->testspec);

	tst = strtok(tdup, ",");
	do {
		char *p, *tlookup;
		int i, atype = dns_name_type("A"), aclass = dns_name_class("IN");

		p = strchr(tst, ':');
		tlookup = (p ? p+1 : tst);
		if (p) { 
			int i;

			*p = '\0'; 
			atype = dns_name_type(tst);
			*p = ':';
		}

		/* Use ares_query() here, since we dont want to get results from hosts file or other odd stuff. */
		ares_query(*((ares_channel *)rec->dnschannel), tlookup, aclass, atype, dns_query_callback, rec);
		tst = strtok(NULL, ",");
	} while (tst);

	xfree(tdup);

	rec->textlog = newstrbuffer(0);
	getntimer(&rec->dnsstarttime);
	rec->dnsstatus = DNS_QUERY_ACTIVE;

	return 1;
}


int dns_add_active_fds(int *maxfd, fd_set *fdread, fd_set *fdwrite)
{
	myconn_t *walk;
	int n, activecount = 0;

	for (walk = dnshead; (walk); walk = walk->dnsnext) {
		if (walk->dnsstatus != DNS_QUERY_ACTIVE) continue;

		activecount++;
		n = ares_fds(*((ares_channel *)walk->dnschannel), fdread, fdwrite);
		if (n > *maxfd) *maxfd = n;
	}

	return activecount;
}

void dns_process_active(fd_set *fdread, fd_set *fdwrite)
{
	myconn_t *walk;

	for (walk = dnshead; (walk); walk = walk->dnsnext) {
		if (walk->dnsstatus != DNS_QUERY_ACTIVE) continue;

		ares_process(*((ares_channel *)walk->dnschannel), fdread, fdwrite);
	}
}

int dns_trimactive(void)
{
	myconn_t *walk;
	int result = 0;

	for (walk = dnshead; (walk); walk = walk->dnsnext) {
		if (walk->dnsstatus != DNS_QUERY_COMPLETED) {
			result++;
			continue;
		}

		ares_destroy(*((ares_channel *)walk->dnschannel));
		walk->dnsstatus = DNS_FINISHED;
	}

	return result;
}

