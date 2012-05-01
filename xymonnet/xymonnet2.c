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

int concurrency = 10;
int defaulttimeout = 30;

int main(int argc, char **argv)
{
	int argi;
	listhead_t *resulthead = NULL;

	for (argi=1; (argi < argc); argi++) {
		if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
	}

	if (debug) conn_register_infohandler(NULL, 7);

	init_tcp_testmodule();

	setup_tests(defaulttimeout);
	resulthead = run_net_tests(concurrency);
	send_test_results(resulthead, programname, 0);

	add_to_sub_queue(NULL, NULL);	/* Set off the submodule tests */

	conn_deinit();
	dns_lookup_shutdown();

	return 0;
}

