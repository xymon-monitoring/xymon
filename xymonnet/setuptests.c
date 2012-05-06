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


int setup_tests(int defaulttimeout, int pingenabled)
{
	char *location;
	void *hwalk;
	int count = 0;

	if (first_host() == NULL) {
		errprintf("Empty configuration, no hosts to test\n");
		return 0;
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

					options.testtype = NET_TEST_PING;
					options.timeout = defaulttimeout;
					ip = strtok_r(tsdup, ",", &sptr);
					while (ip) {
						/* 
						 * We use 'conn' here as the testspec, and provide the destination IP. 
						 * Default ping test uses 'ping' as the testspec, and does NOT provide the
						 * IP since it uses the default destination (hostname, or IP if 'testip' set).
						 */
						if (conn_is_ip(ip) && (strcmp(ip, destination) != 0) && xymon_sqldb_nettest_due(xmh_item(hwalk, XMH_HOSTNAME), "conn", ip, &options, location)) {
							memset(&netparams, 0, sizeof(netparams));
							netparams.destinationip = strdup(ip);
							add_net_test("ping", NULL, 0, &options, &netparams, hwalk);
							count++;
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
					if (xymon_sqldb_nettest_due(xmh_item(hwalk, XMH_HOSTNAME), testspec, NULL, &options, location)) {
						/* netparams.destinationip may have been filled by net_dialog (e.g. http) */
						if (!netparams.destinationip) netparams.destinationip = strdup(destination);
						add_net_test(testspec, dialog, dtoken, &options, &netparams, hwalk);
						count++;
					}
					else {
						/* Not due yet - clean up what net_dialog() allocated for us */
						if (dialog) free_net_dialog(dialog, dtoken);
						if (netparams.destinationip) xfree(netparams.destinationip);
					}
				}
			}

			testspec = xmh_item_walk(NULL);
		}

		/* 
		 * Add the ping check. We do this last, so any net-options can be picked up also for the ping-test.
		 */
		memset(&netparams, 0, sizeof(netparams));
		netparams.destinationip = strdup(destination);
		options.testtype = NET_TEST_PING;
		if (pingenabled && !xmh_item(hwalk, XMH_FLAG_NOCONN) && !xmh_item(hwalk, XMH_FLAG_NOPING) && xymon_sqldb_nettest_due(xmh_item(hwalk, XMH_HOSTNAME), "ping", NULL, &options, location)) {
			add_net_test("ping", NULL, 0, &options, &netparams, hwalk);
			count++;
		}
		else {
			xfree(netparams.destinationip);
		}
	}

	return count;
}

int setup_tests_fromdb(int defaulttimeout, int pingenabled)
{
	char *location, *hostname, *testspec, *destination;
	myconn_netparams_t netparams;
	net_test_options_t options;
	int count = 0;

	/* See what network we'll test */
	location = xgetenv("XYMONNETWORK");
	if (strlen(location) == 0) location = NULL;

	load_protocols(NULL);
	load_cookies();

	while (xymon_sqldb_nettest_row(location, &hostname, &testspec, &destination, &options)) {
		void *hwalk;
		char **dialog;
		int dtoken;

		count++;

		hwalk = hostinfo(hostname);
		if (!hwalk) continue;

		if (!destination || (*destination == '\0')) {
			destination = xmh_item(hwalk, XMH_HOSTNAME);

			if (xmh_item(hwalk, XMH_FLAG_TESTIP) && !conn_null_ip(xmh_item(hwalk, XMH_IP))) {
				destination = xmh_item(hwalk, XMH_IP);
			}
		}

		memset(&netparams, 0, sizeof(netparams));
		if (options.testtype == NET_TEST_PING) {
			netparams.destinationip = strdup(destination);
			add_net_test("ping", NULL, 0, &options, &netparams, hwalk);
			/* The default "ping" has a NULL destination in the table; the "conn" test has the IP as destination */
			if (strcmp(testspec, "ping") == 0) {
				xymon_sqldb_nettest_rowupdate(xmh_item(hwalk, XMH_HOSTNAME), "ping", NULL);
			}
			else {
				xymon_sqldb_nettest_rowupdate(xmh_item(hwalk, XMH_HOSTNAME), "conn", destination);
			}
				
		}
		else {
			dialog = net_dialog(testspec, &netparams, &options, hwalk, &dtoken);
			/* netparams.destinationip may have been filled by net_dialog (e.g. http) */
			if (!netparams.destinationip) netparams.destinationip = strdup(destination);
			add_net_test(testspec, dialog, dtoken, &options, &netparams, hwalk);
			xymon_sqldb_nettest_rowupdate(xmh_item(hwalk, XMH_HOSTNAME), testspec, NULL);
		}
	}

	return count;
}

