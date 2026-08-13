/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/server/alert-escalation-repeat-harness.c
 *
 * Driver for clear_interval() at a severity increase, used by
 * alert-escalation-repeat.sh.
 *
 * A repeat interval is keyed by hostname|testname|method|recipient
 * (do_alert.c), not by the rule that produced it, so the same MAIL address
 * under a yellow rule and under a red one shares ONE record. When the colour
 * escalates, xymond_alert calls clear_interval() so the interval left by the
 * old colour does not hold back the new one.
 *
 * clear_interval() finds its recipients through next_recipient(), which is
 * also what decides whether an alert may be sent -- and FOR= makes that answer
 * "not yet". Without alert_ignore_holdtime() the walk therefore returns
 * nobody at the very moment the state has to be invalidated, the record
 * survives, and the red alert is dropped until the yellow's interval comes
 * due: the transition FOR= exists to damp is the one it delays.
 *
 * do_alert.c is #included rather than linked: find_repeatinfo() and the repeat
 * list are static, and this test is about what happens to a record inside it.
 *
 * usage: harness <hosts.cfg> <alerts.cfg> <repeat-due-in-sec>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libxymon.h"

#include "do_alert.c"

int main(int argc, char *argv[])
{
	activealerts_t alert;
	time_t now = getcurrenttime(NULL);
	recip_t *recip;
	repeat_t *rpt;
	int first = 1, duein;

	if (argc < 4) {
		fprintf(stderr, "usage: %s <hosts.cfg> <alerts.cfg> <repeat-due-in-sec>\n", argv[0]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());
	if (load_alertconfig(argv[2], (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE), 0) == 0) {
		fprintf(stderr, "cannot load %s\n", argv[2]);
		return 2;
	}
	duein = atoi(argv[3]);

	memset(&alert, 0, sizeof(alert));
	alert.hostname   = strdup("testhost");
	alert.testname   = strdup("conn");
	alert.location   = strdup("");
	alert.osname     = strdup("");
	alert.classname  = strdup("");
	alert.groups     = strdup("");
	strcpy(alert.ip, "127.0.0.1");
	alert.state      = A_PAGING;

	/* The yellow alert: it went out a while ago and set a repeat interval. */
	alert.color = alert.maxcolor = COL_YELLOW;
	alert.eventstart = alert.colorstart = now - 3600;
	stoprulefound = 0;
	recip = next_recipient(&alert, &first, NULL, NULL);
	if (!recip) {
		fprintf(stderr, "no recipient for the yellow rule -- the fixture is wrong\n");
		return 2;
	}
	rpt = find_repeatinfo(&alert, recip, 1);
	if (!rpt) {
		fprintf(stderr, "no repeat record created\n");
		return 2;
	}
	rpt->nextalert = now + duein;

	/*
	 * Now red begins. colorstart moves to this instant, as xymond_alert sets
	 * it from the message's lastchange, so a FOR= on the red rule is not
	 * satisfied yet. This is the escalation path: severity increased, so the
	 * repeat interval must be cleared.
	 */
	alert.color = alert.maxcolor = COL_RED;
	alert.colorstart = now;
	clear_interval(&alert);

	printf("nextalert=%s\n", ((rpt->nextalert == 0) ? "cleared" : "kept"));
	return 0;
}
