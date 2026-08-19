/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Creates one RRD whose last reading is at a chosen time, for
 * tests/web/showgraph-stale-filter.sh. xymond_rrd cannot: only creation
 * ("-b") can be backdated. The shape matches do_disk.c's, whose "pct" DS
 * the graph definition under test reads by name.
 *
 * usage: harness <file> <epoch-of-last-reading>
 */
#include <stdio.h>
#include <stdlib.h>
#include "rrd_api_compat.h"	/* rrd.h argv is char** or const char** by version */

/* argc for a NULL-terminated argv initializer, so nobody counts by hand */
#define NARGS(a) ((int)(sizeof(a) / sizeof((a)[0])) - 1)

int main(int argc, char **argv)
{
	char start[32], value[64];
	long last;
	xymon_rrd_argv_item_t cargv[] = { "rrdcreate", NULL, "-b", start, "-s", "300",
		"DS:pct:GAUGE:600:0:100", "DS:used:GAUGE:600:0:U",
		"RRA:AVERAGE:0.5:1:576", "RRA:AVERAGE:0.5:6:576",
		"RRA:AVERAGE:0.5:24:576", "RRA:AVERAGE:0.5:288:576", NULL };
	xymon_rrd_argv_item_t uargv[] = { "rrdupdate", NULL, value, NULL };

	if (argc != 3) { fprintf(stderr, "usage: %s <file> <epoch>\n", argv[0]); return 2; }
	last = atol(argv[2]);
	cargv[1] = uargv[1] = argv[1];
	snprintf(start, sizeof(start), "%ld", last - 300);
	snprintf(value, sizeof(value), "%ld:40:400000", last);

	if (xymon_rrd_create(NARGS(cargv), cargv) != 0) { fprintf(stderr, "create: %s\n", rrd_get_error()); return 1; }

	if (xymon_rrd_update(NARGS(uargv), uargv) != 0) { fprintf(stderr, "update: %s\n", rrd_get_error()); return 1; }

	return 0;
}
