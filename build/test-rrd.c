#include "rrd_compat.h"

int main(void)
{
	/* Compile-time ABI surface checks for all wrapped RRDtool entry points. */
	xymon_rrd_argv_item_t graphargs[] = {
		"rrdgraph",
		"xymongen.png",
		"-s", "e - 48d",
		"--title", "xymongen runtime last 48 days",
		"-w576",
		"-v", "Seconds",
		"-a", "PNG",
		"DEF:rt=xymongen.rrd:runtime:AVERAGE",
		"AREA:rt#00CCCC:Run Time",
		"COMMENT: Timestamp",
		NULL
	};
	xymon_rrd_argv_item_t updateargs[] = { "rrdupdate", "dummy.rrd", NULL };
	xymon_rrd_argv_item_t createargs[] = { "rrdcreate", "dummy.rrd", NULL };
	xymon_rrd_argv_item_t fetchargs[]  = { "rrdfetch", "dummy.rrd", NULL };
	char **calcpr=NULL;

	int pcount, xsize, ysize;
	double ymin, ymax;
	time_t start = 0, end = 0;
	unsigned long step = 0, dscount = 0;
	char **dsnames = NULL;
	rrd_value_t *data = NULL;

	for (pcount = 0; (graphargs[pcount]); pcount++);
	rrd_clear_error();
	/* We only need these calls to type-check against the active RRDtool headers. */
	(void)xymon_rrd_update(3, updateargs);
	(void)xymon_rrd_create(3, createargs);
	(void)xymon_rrd_fetch(3, fetchargs, &start, &end, &step, &dscount, &dsnames, &data);
	/* Keep one graph invocation to validate the graph ABI shape as well. */
	(void)xymon_rrd_graph(pcount, graphargs, &calcpr, &xsize, &ysize, &ymin, &ymax);

	return 0;
}
