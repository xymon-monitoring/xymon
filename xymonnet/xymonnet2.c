/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2011-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdio.h>
#include <signal.h>

#include "libxymon.h"
#include "setuptests.h"
#include "tcptalk.h"
#include "dnstalk.h"
#include "sendresults.h"
#include "netsql.h"

#define DEF_TIMEOUT 30

time_t lastloadtime = 0;
int running = 1;
int flushdbdata = 0;

int concurrency = 0;
int defaulttimeout = DEF_TIMEOUT;
char *defaultsourceip4 = NULL, *defaultsourceip6 = NULL;
int pingenabled = 1;


void sig_handler(int signum)
{
	switch (signum) {
	  case SIGHUP:
		lastloadtime = 0;
		flushdbdata = 1;
		break;
	  case SIGINT:
	  case SIGTERM:
		running = 0;
	}
}


int run_tests(void)
{
	int count;
	listhead_t *resulthead = NULL;

	if (flushdbdata) {
		xymon_sqldb_flushall();
		flushdbdata = 0;
	}

	count = setup_tests(defaulttimeout, pingenabled);
	if (count > 0) {
		resulthead = run_net_tests(concurrency, defaultsourceip4, defaultsourceip6);
		count = resulthead->len;
		send_test_results(resulthead, programname, 0);
		add_to_sub_queue(NULL, NULL);	/* Set off the submodule tests */
		cleanup_myconn_list(resulthead);
	}

	return count;
}


int main(int argc, char **argv)
{
	int argi;
	time_t nextrun = 0;

	for (argi=1; (argi < argc); argi++) {
		if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
		else if (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=');
			defaulttimeout = atoi(p+1);
			if (defaulttimeout <= 0) defaulttimeout = DEF_TIMEOUT;
		}
		else if (argnmatch(argv[argi], "--concurrency=")) {
			char *p = strchr(argv[argi], '=');
			concurrency = atoi(p+1);
			if (concurrency <= 0) concurrency = 0;
		}
		else if (strcmp(argv[argi], "--test-untagged") == 0) {
			test_nonet_hosts(1);
		}
		else if (argnmatch(argv[argi], "--dns=")) {
			char *p = strchr(argv[argi], '=');
			if (strcmp(p+1, "ip") == 0) set_dns_strategy(DNS_STRATEGY_IP);
			else if (strcmp(p+1, "only") == 0) set_dns_strategy(DNS_STRATEGY_HOSTNAME);
			else set_dns_strategy(DNS_STRATEGY_STANDARD);
		}
		else if (argnmatch(argv[argi], "--source-ip=")) {
			char *p = strchr(argv[argi], '=');
			switch (conn_is_ip(p+1)) {
			  case 4: defaultsourceip4 = p; break;
			  case 6: defaultsourceip6 = p; break;
			}
		}
		else if ((strcmp(argv[argi], "--noping") == 0) || (strcmp(argv[argi], "--no-ping") == 0)) {
			pingenabled = 0;
		}
		else if (strcmp(argv[argi], "--once") == 0) {
			running = 0;
		}
		else if ((strcmp(argv[argi], "--wipedb") == 0) || (strcmp(argv[argi], "--wipe-db") == 0)) {
			flushdbdata = 1;
		}
	}

	if (debug) conn_register_infohandler(NULL, 7);

	{
		struct sigaction sa;

		setup_signalhandler(programname);
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = sig_handler;
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGHUP, &sa, NULL);
	}

	if (xymon_sqldb_init() != 0) {
		errprintf("Cannot open Xymon SQLite database - aborting\n");
		return 1;
	}

	init_tcp_testmodule();

	do {
		int testcount;
		time_t now = gettimer();

		if (now > (lastloadtime+600)) {
			lastloadtime = now;

			if (load_hostnames("@", NULL, get_fqdn()) != 0) {
				errprintf("Cannot load host configuration from xymond\n");

				if (load_hostnames(xgetenv("HOSTSCFG"), "netinclude", get_fqdn()) != 0) {
					errprintf("Cannot load host configuration from %s\n", xgetenv("HOSTSCFG"));
					return;
				}
			}
		}

		dbgprintf("Launching tests\n");
		testcount = run_tests();
		dbgprintf("Ran %d tests\n", testcount);

		if (running) sleep(testcount ? 15 : 30);	/* Take a nap */
	} while (running);

	conn_deinit();
	xymon_sqldb_shutdown();

	return 0;
}

