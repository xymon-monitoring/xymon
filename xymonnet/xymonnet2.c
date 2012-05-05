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

#include "libxymon.h"
#include "setuptests.h"
#include "tcptalk.h"
#include "dnstalk.h"
#include "sendresults.h"
#include "netsql.h"

#define DEF_TIMEOUT 30

int concurrency = 0;
int defaulttimeout = DEF_TIMEOUT;
char *defaultsourceip4 = NULL, *defaultsourceip6 = NULL;
int pingenabled = 1;


void do_tests(void)
{
	listhead_t *resulthead = NULL;

	setup_tests(defaulttimeout, pingenabled);
	resulthead = run_net_tests(concurrency, defaultsourceip4, defaultsourceip6);
	send_test_results(resulthead, programname, 0);
	add_to_sub_queue(NULL, NULL);	/* Set off the submodule tests */
}


int main(int argc, char **argv)
{
	int argi;

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
	}

	if (debug) conn_register_infohandler(NULL, 7);

	if (xymon_sqldb_init() != 0) {
		errprintf("Cannot open Xymon SQLite database - aborting\n");
		return 1;
	}

	init_tcp_testmodule();

	do_tests();

	conn_deinit();
	xymon_sqldb_shutdown();

	return 0;
}

