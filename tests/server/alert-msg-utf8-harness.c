/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/server/alert-msg-utf8-harness.c
 *
 * Driver for send_alert() with a SCRIPT recipient, used by
 * alert-msg-utf8.sh: sends one red alert for "testhost" whose message is
 * argv[3], so the script it starts records BBALPHAMSG. With MAXMSG_ALERTSCRIPT
 * small the message is cut; the test checks the cut did not split a UTF-8
 * character. do_alert.c is #included, as in alert-numeric-codes-harness.c.
 *
 * usage: harness <hosts.cfg> <alerts.cfg> <message>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "do_alert.c"

int main(int argc, char *argv[])
{
	activealerts_t alert;

	if (argc < 4) {
		fprintf(stderr, "usage: %s <hosts.cfg> <alerts.cfg> <message>\n", argv[0]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());
	if (load_alertconfig(argv[2], (1 << COL_RED), 0) == 0) {
		fprintf(stderr, "cannot load %s\n", argv[2]);
		return 2;
	}

	memset(&alert, 0, sizeof(alert));
	alert.hostname   = strdup("testhost");
	alert.testname   = strdup("conn");
	alert.location   = strdup("");
	alert.osname     = strdup("");
	alert.classname  = strdup("");
	alert.groups     = strdup("");
	snprintf(alert.ip, sizeof(alert.ip), "%s", "1.2.3.4");
	alert.pagemessage = (unsigned char *)strdup(argv[3]);
	alert.color = alert.maxcolor = COL_RED;
	alert.eventstart = alert.colorstart = getcurrenttime(NULL) - 3600;
	alert.state = A_PAGING;
	alert.cookie = 12345;

	start_alerts();
	send_alert(&alert, NULL);
	finish_alerts();
	return 0;
}
