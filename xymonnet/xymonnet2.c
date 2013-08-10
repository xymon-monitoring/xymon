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
#include "netdialog.h"
#include "sendresults.h"
#include "netsql.h"

#define DEF_TIMEOUT 30

time_t lastloadtime = 0;
int running = 1;
char *location = NULL;

int concurrency = 0;
int defaulttimeout = DEF_TIMEOUT;
char *defaultsourceip4 = NULL, *defaultsourceip6 = NULL;
int pingenabled = 1;
int usebackfeedqueue = 0;

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGHUP:
		lastloadtime = 0;
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

	count = setup_tests_from_database(pingenabled, (running == 0));
	if (count > 0) {
		resulthead = run_net_tests(concurrency, defaultsourceip4, defaultsourceip6);
		count = resulthead->len;
		send_test_results(resulthead, programname, 0, location, usebackfeedqueue);
		cleanup_myconn_list(resulthead);
	}

	return count;
}


int main(int argc, char **argv)
{
	int argi;
	time_t nextrun = 0;
	int wipedb = 0;

	libxymon_init(argv[0]);
	for (argi=1; (argi < argc); argi++) {
		if (standardoption(argv[argi])) {
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
		else if (strcmp(argv[argi], "--net-debug") == 0) {
			conn_register_infohandler(NULL, 7);
		}
		else if ((strcmp(argv[argi], "--wipedb") == 0) || (strcmp(argv[argi], "--wipe-db") == 0)) {
			wipedb = 1;
		}
		else if (*(argv[argi]) != '-') {
			add_wanted_host(argv[argi]);
			running = 0;	/* When testing specific hosts, assume "--once" */
		}
	}


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

	if (wipedb) {
		xymon_sqldb_flushall();
		xymon_sqldb_shutdown();
		errprintf("Xymon net-test database wiped\n");
		return 0;
	}

	init_tcp_testmodule();
	usebackfeedqueue = (sendmessage_init_local() > 0);

	/* See what network we'll test */
	location = xgetenv("XYMONNETWORK");
	if (strlen(location) == 0) location = NULL;

	do {
		int testcount;
		time_t now = gettimer();

		if (now > (lastloadtime+600)) {
			lastloadtime = now;
			read_tests_from_hostscfg(defaulttimeout);
			load_protocols(NULL);
		}

		dbgprintf("Launching tests\n");
		testcount = run_tests();
		dbgprintf("Ran %d tests\n", testcount);

		if (running) {
			int timetonext;

			xymon_sqldb_sanitycheck();
			timetonext = xymon_sqldb_secs_to_next_test();
			if (timetonext > 0) {
				timetonext++;
				if (timetonext > 60) timetonext = 60;
				dbgprintf("Sleeping %d seconds\n", timetonext);
				sleep(timetonext);
			}
		}
	} while (running);

	if (usebackfeedqueue) sendmessage_finish_local();
	conn_deinit();
	xymon_sqldb_shutdown();

	return 0;
}

