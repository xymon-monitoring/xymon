/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns2.c 6743 2011-09-03 15:44:52Z storner $";

#include <string.h>
#include <stdlib.h>

#include "libxymon.h"
#include "httpcookies.h"
#include "tcptalk.h"
#include "netdialog.h"
#include "setuptests.h"

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

void setup_tests(int defaulttimeout)
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

	/* update nettest set valid=0 */
	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		char *destination, *testspec;
		myconn_netparams_t netparams;
		net_test_options_t options;
		char **dialog;

		if (!wanted_host(hwalk, location)) continue;

		destination = xmh_item(hwalk, XMH_HOSTNAME);

		if (xmh_item(hwalk, XMH_FLAG_TESTIP) && !conn_null_ip(xmh_item(hwalk, XMH_IP))) {
			destination = xmh_item(hwalk, XMH_IP);
		}

		if (!xmh_item(hwalk, XMH_FLAG_NOCONN) && !xmh_item(hwalk, XMH_FLAG_NOPING)) {
			/* Add the ping check */
			memset(&netparams, 0, sizeof(netparams));
			netparams.destinationip = strdup(destination);
			options.testtype = NET_TEST_PING;
			options.timeout = defaulttimeout;
			/* update nettest set valid=1 where hostname=xmh_item(hwalk, XMH_HOSTNAME) and testspec="ping" */
			add_net_test("ping", NULL, &options, &netparams, hwalk);
		}

		testspec = xmh_item_walk(hwalk);
		while (testspec) {
			net_test_options_t options = { NET_TEST_STANDARD, defaulttimeout };

			memset(&netparams, 0, sizeof(netparams));
			dialog = net_dialog(testspec, &netparams, &options, hwalk);

			if (dialog || (options.testtype != NET_TEST_STANDARD)) {
				/* insert into nettests / update nettest set valid=1 where hostname=xmh_item(hwalk, XMH_HOSTNAME) and testspec=testspec */
				/* destinationip may have been filled by net_dialog (e.g. http) */
				if (!netparams.destinationip) netparams.destinationip = strdup(destination);
				add_net_test(testspec, dialog, &options, &netparams, hwalk);
			}

			testspec = xmh_item_walk(NULL);
		}
	}

	/* delete from nettests where valid=0 */
	/* commit */

	/* select hostname,testspec from nettests where valid=1 and lastrun+interval < now
	 * Calc hwalk, dialog, netparams - add_net_test() */
}

