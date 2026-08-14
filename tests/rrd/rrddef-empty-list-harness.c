/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * tests/rrd/rrddef-empty-list-harness.c
 *
 * rrd_setup() is static, so it is reached the way every caller reaches it: the
 * first find_xymon_rrd()/find_xymon_graph() builds both tables. The caller
 * sets TEST2RRD, GRAPHS and the TCP service list empty -- the case that read
 * past a 1-byte allocation while counting commas.
 */

#include <stdio.h>

#include "libxymon.h"

int main(void)
{
	xymonrrd_t *rrd;
	xymongraph_t *graph;

	/* Neither lookup can succeed against empty tables; building them is the
	 * point. */
	rrd = find_xymon_rrd("nosuchservice", "");
	graph = find_xymon_graph("nosuchgraph");

	printf("rrd=%s graph=%s\n", (rrd ? "found" : "none"), (graph ? "found" : "none"));
	return 0;
}
