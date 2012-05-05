/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>

#include "libxymon.h"
#include "httpcookies.h"
#include "tcptalk.h"
#include "netdialog.h"
#include "setuptests.h"
#include "netsql.h"

#define DEFAULT_NET_INTERVAL 300

static int testuntagged = 0;
static char **selectedhosts = NULL;
static int shsz = 0;

void test_nonet_hosts(int testthem)
{
	testuntagged = testthem;
}

void add_wanted_host(char *hostname)
{

	shsz++;
	selectedhosts = (char **)realloc(selectedhosts, (shsz+1)*sizeof(char *));
	selectedhosts[shsz-1] = strdup(hostname);
	selectedhosts[shsz] = NULL;
}

void clear_wanted_hosts(void)
{
	int i;

	for (i=0; (selectedhosts[i]); i++) xfree(selectedhosts[i]);
	xfree(selectedhosts);
	selectedhosts = NULL;
	shsz = 0;
}

static int wanted_host(void *host, char *netstring)
{

	if (!selectedhosts) {
		char *netlocation = xmh_item(host, XMH_NET);

		return ( !netstring || (strlen(netstring) == 0) ||                  /* No XYMONNETWORK = do all */
			 (netlocation && (strcmp(netlocation, netstring) == 0)) ||  /* XYMONNETWORK && matching NET: tag */
			 (testuntagged && (netlocation == NULL)) );                 /* No NET: tag for this host */
	}
	else {
		/* User provided an explicit list of hosts to test */
		int i;

		for (i=0; selectedhosts[i]; i++) {
			if (strcasecmp(selectedhosts[i], xmh_item(host, XMH_HOSTNAME)) == 0) return 1;
		}
	}

	return 0;
}


void setup_tests(int defaulttimeout, int pingenabled)
{
	char *location;
	void *hwalk;

	if (load_hostnames("@", NULL, get_fqdn()) != 0) {
		errprintf("Cannot load host configuration from xymond\n");

		if (load_hostnames(xgetenv("HOSTSCFG"), "netinclude", get_fqdn()) != 0) {
			errprintf("Cannot load host configuration from %s\n", xgetenv("HOSTSCFG"));
			return;
		}
	}

	if (first_host() == NULL) {
		errprintf("Empty configuration, no hosts to test\n");
		return;
	}

	/* See what network we'll test */
	location = xgetenv("XYMONNETWORK");
	if (strlen(location) == 0) location = NULL;

	load_protocols(NULL);
	load_cookies();

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		char *destination, *testspec;
		myconn_netparams_t netparams;
		net_test_options_t options;

		if (!wanted_host(hwalk, location)) continue;

		options.timeout = defaulttimeout;
		options.interval = DEFAULT_NET_INTERVAL;
		options.sourceip = NULL;

		destination = xmh_item(hwalk, XMH_HOSTNAME);

		if (xmh_item(hwalk, XMH_FLAG_TESTIP) && !conn_null_ip(xmh_item(hwalk, XMH_IP))) {
			destination = xmh_item(hwalk, XMH_IP);
		}

		if (pingenabled && !xmh_item(hwalk, XMH_FLAG_NOCONN) && !xmh_item(hwalk, XMH_FLAG_NOPING)) {
			/* Add the ping check */
			memset(&netparams, 0, sizeof(netparams));
			netparams.destinationip = strdup(destination);
			options.testtype = NET_TEST_PING;
			add_net_test("ping", NULL, 0, &options, &netparams, hwalk);
		}

		testspec = xmh_item_walk(hwalk);
		while (testspec) {
			char **dialog;
			int dtoken;

			options.testtype = NET_TEST_STANDARD;
			if (argnmatch(testspec, "nopt=") || argnmatch(testspec, "nopt:")) {
				char *allopts, *opt, *sptr;

				allopts = strdup(testspec+5);
				opt = strtok_r(allopts, ",", &sptr);
				while (opt) {
					if (argnmatch(opt, "timeout")) options.timeout = atoi(opt+8);
					else if (argnmatch(opt, "interval")) options.interval = atoi(opt+9);
					else if (argnmatch(opt, "sourceip")) options.sourceip = strdup(opt+9);

					opt = strtok_r(NULL, ",", &sptr);
				}
				xfree(allopts);
			}
			else if (strncmp(testspec, "conn=", 5) == 0) {
				if (pingenabled) {
					char *tsdup = strdup(testspec+5);
					char *sptr, *ip;

					ip = strtok_r(tsdup, ",", &sptr);
					while (ip) {
						if (conn_is_ip(ip) && (strcmp(ip, destination) != 0)) {
							memset(&netparams, 0, sizeof(netparams));
							netparams.destinationip = strdup(ip);
							options.testtype = NET_TEST_PING;
							options.timeout = defaulttimeout;
							add_net_test("ping", NULL, 0, &options, &netparams, hwalk);
						}

						ip = strtok_r(NULL, ",", &sptr);
					}

					xfree(tsdup);
				}
			}
			else {
				memset(&netparams, 0, sizeof(netparams));
				dialog = net_dialog(testspec, &netparams, &options, hwalk, &dtoken);

				if (dialog || (options.testtype != NET_TEST_STANDARD)) {
					if (xymon_sqldb_nettest_due(xmh_item(hwalk, XMH_HOSTNAME), testspec, options.interval)) {
						/* destinationip may have been filled by net_dialog (e.g. http) */
						if (!netparams.destinationip) netparams.destinationip = strdup(destination);
						add_net_test(testspec, dialog, dtoken, &options, &netparams, hwalk);
					}
					else {
						/* Not due yet - clean up what net_dialog() allocated for us */
						if (dialog) free_net_dialog(dialog, dtoken);
					}
				}
			}

			testspec = xmh_item_walk(NULL);
		}
	}
}

