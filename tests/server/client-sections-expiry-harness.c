/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * tests/server/client-sections-expiry-harness.c
 *
 * Drives the real clientmsg_refresh() and update_clientsections(), extracted
 * from xymond/xymond.c into extracted.inc by the test script. Both are static
 * and xymond has no library form, so this is the same shape as the other
 * xymond static tests.
 *
 * Only the fields the extracted code touches are declared here; the timestamp
 * is supplied by the caller, since waiting out MAX_SUBCLIENT_LIFETIME is not
 * something a test can do.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libxymon.h"

typedef struct clientmsg_list_t {
	char *collectorid;
	char *msg;
	time_t timestamp;
	char *sections;
	struct clientmsg_list_t *next;
} clientmsg_list_t;

typedef struct xymond_hostlist_t {
	char *hostname;
} xymond_hostlist_t;

#include "extracted.inc"

/* One report cycle: the collector delivers msg, which is adopted and compared
 * against whatever baseline the entry carries. Mirrors handle_client(). */
static void report(xymond_hostlist_t *hwalk, clientmsg_list_t *cwalk, char *msg, time_t now)
{
	if (cwalk->msg) xfree(cwalk->msg);
	cwalk->msg = strdup(msg);
	clientmsg_refresh(cwalk, now);
	update_clientsections(hwalk, cwalk);
}

int main(int argc, char **argv)
{
	xymond_hostlist_t host;
	clientmsg_list_t entry;
	time_t now = 1786000000;
	char *scenario = (argc > 1) ? argv[1] : "";

	memset(&host, 0, sizeof(host));
	memset(&entry, 0, sizeof(entry));
	host.hostname = "testhost";
	entry.collectorid = "";

	if (strcmp(scenario, "expired") == 0) {
		/* The collector reports [old] and [keep], then goes away for longer
		 * than MAX_SUBCLIENT_LIFETIME and comes back without [old]. Its
		 * entry would have been purged in between had the report not
		 * arrived first, so nothing may be announced as vanished. */
		report(&host, &entry, "\n[old]\ndata\n[keep]\ndata\n", now);
		report(&host, &entry, "\n[keep]\ndata\n", now + MAX_SUBCLIENT_LIFETIME + 1);
	}
	else if (strcmp(scenario, "live") == 0) {
		/* The same removal between two reports that are close together is a
		 * real change and must be announced - exactly once. */
		report(&host, &entry, "\n[old]\ndata\n[keep]\ndata\n", now);
		report(&host, &entry, "\n[keep]\ndata\n", now + 60);
	}
	else if (strcmp(scenario, "expired-then-live") == 0) {
		/* After a return from expiry the new report is the baseline, so a
		 * removal after it is still announced. */
		report(&host, &entry, "\n[old]\ndata\n[keep]\ndata\n", now);
		report(&host, &entry, "\n[keep]\ndata\n[extra]\ndata\n",
		       now + MAX_SUBCLIENT_LIFETIME + 1);
		report(&host, &entry, "\n[keep]\ndata\n", now + MAX_SUBCLIENT_LIFETIME + 61);
	}
	else {
		fprintf(stderr, "unknown scenario '%s'\n", scenario);
		return 2;
	}

	return 0;
}
