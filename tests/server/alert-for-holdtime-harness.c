/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/server/alert-for-holdtime-harness.c
 *
 * Driver for the FOR=N alerts.cfg criterion (issue #229, step 3), used by
 * alert-for-holdtime.sh.
 *
 * FOR=N matches when the rule's colour has held for N minutes. It is the
 * counterpart of DURATION>N, which measures the age of the alert *event* and
 * deliberately survives yellow->red -- so a red arriving after two hours of
 * yellow satisfies DURATION>10 immediately, and FOR=10 must not.
 *
 * The harness builds one activealerts_t with the two timestamps set
 * independently, loads the alerts.cfg passed on argv, and prints MATCH or
 * NOMATCH from have_recipient(). Setting eventstart and colorstart apart is
 * the whole point: a build that reads eventstart where it should read
 * colorstart prints MATCH for the yellow->red case, and the shell names it.
 *
 * With "report" as a sixth argument it prints what the config report shows
 * for the same alert instead (svcstatus.cgi, confreport.cgi), which is where
 * a hold time computed one way and displayed another becomes visible.
 *
 * usage: harness <hosts.cfg> <alerts.cfg> <color> <colorage-sec> <eventage-sec> [report]
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
	int colorage, eventage;
	int anymatch = 0, matched;

	if (argc < 6) {
		fprintf(stderr, "usage: %s <hosts.cfg> <alerts.cfg> <color> <colorage-sec> <eventage-sec>\n", argv[0]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());

	/*
	 * alertcolors: the set a rule matches when it names no COLOR= of its own.
	 * red|yellow|purple is what xymond_alert defaults to, and FOR must refuse
	 * that set rather than pick one of them.
	 */
	/* Returns 1 when it loaded, 0 when it could not open the file (the
	   "unchanged, skipped" 0 needs a second call in one process). */
	if (load_alertconfig(argv[2], (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE), 0) == 0) {
		fprintf(stderr, "cannot load %s\n", argv[2]);
		return 2;
	}

	colorage = atoi(argv[4]);
	eventage = atoi(argv[5]);

	memset(&alert, 0, sizeof(alert));
	alert.hostname   = strdup("testhost");
	alert.testname   = strdup("conn");
	alert.location   = strdup("");
	alert.osname     = strdup("");
	alert.classname  = strdup("");
	alert.groups     = strdup("");
	strcpy(alert.ip, "127.0.0.1");
	alert.color      = alert.maxcolor = parse_color(argv[3]);
	alert.eventstart = now - eventage;
	alert.colorstart = now - colorage;
	alert.state      = A_PAGING;
	alert.next       = NULL;

	/*
	 * anymatch is what tells "nobody is due yet" from "no rule covers this
	 * alert at all". xymond_alert (xymond_alert.c:941-948) sends an alert
	 * with no recipient AND no match to A_NORECIP and calls cleanup_alert(),
	 * discarding the repeat state -- and with it the recovery message. A
	 * criterion that merely defers must leave it set, so the shell asserts
	 * on it and not only on the verdict.
	 */
	if ((argc > 6) && (strcmp(argv[6], "report") == 0)) {
		/*
		 * printmode is what the CGIs set, and it is the mode in which an
		 * unsatisfied hold time reports the delay instead of withholding
		 * the row (loadalerts.c:1151) -- so this prints the same recipient
		 * a deferred alert has, with the delay it is waiting on.
		 */
		strbuffer_t *buf = newstrbuffer(0);

		alert_printmode(1);
		print_alert_recipients(&alert, buf);
		fputs(STRBUF(buf), stdout);
		return 0;
	}

	/* Sequenced deliberately: argument evaluation order is unspecified, so
	   reading anymatch inside the same printf() can print it before the call
	   that sets it. */
	matched = have_recipient(&alert, &anymatch);
	printf("%s anymatch=%d\n", (matched ? "MATCH" : "NOMATCH"), anymatch);
	return 0;
}
