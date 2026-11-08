/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/server/alerts-page-undecided-harness.c
 *
 * Driver for the PAGE= filter in criteriamatch() (lib/loadalerts.c), used by
 * alerts-page-undecided.sh -- issue #529.
 *
 * criteriamatch() is static, so the probe goes through next_recipient(), the
 * public entry that decides who an alert goes to. The alert's location is taken
 * from argv rather than from a host, because that is the field under test: the
 * question is what a PAGE= rule does when the location it is matched against
 * yields no token at all, which no hosts.cfg can express.
 *
 * Prints the number of recipients the alert would reach. The scenarios and the
 * expected counts live in the shell script, which owns the pass/fail decision.
 *
 * usage: harness <hosts.cfg> <alerts.cfg> <location>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libxymon.h"

int main(int argc, char *argv[])
{
	activealerts_t alert;
	time_t now = getcurrenttime(NULL);
	recip_t *recip;
	int first = 1, count = 0;

	if (argc < 4) {
		fprintf(stderr, "usage: %s <hosts.cfg> <alerts.cfg> <location>\n", argv[0]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());
	if (load_alertconfig(argv[2], (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE), 0) == 0) {
		fprintf(stderr, "cannot load %s\n", argv[2]);
		return 2;
	}

	memset(&alert, 0, sizeof(alert));
	alert.hostname   = strdup("testhost");
	alert.testname   = strdup("conn");
	alert.location   = strdup(argv[3]);
	alert.osname     = strdup("");
	alert.classname  = strdup("");
	alert.groups     = strdup("");
	strcpy(alert.ip, "127.0.0.1");
	alert.state      = A_PAGING;
	alert.color      = alert.maxcolor = COL_RED;
	alert.eventstart = alert.colorstart = now - 3600;

	/*
	 * !stoprulefound is how every production caller walks this (do_alert.c),
	 * so a fixture using STOP is counted the way xymond_alert would count it.
	 */
	stoprulefound = 0;
	while (!stoprulefound && ((recip = next_recipient(&alert, &first, NULL, NULL)) != NULL))
		count++;

	/*
	 * Keyed rather than bare: criteriamatch() logprintf's to stdout when the
	 * alert's host is not in hosts.cfg, and logprintf is plain printf
	 * (lib/errormsg.h), so the count has to be findable among other lines.
	 */
	printf("recipients=%d\n", count);
	return 0;
}
