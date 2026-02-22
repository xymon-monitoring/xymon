#include "rrd_compat.h"

int main(void)
{
	/* Compile/link probe target for the compat graph wrapper. */
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
	char **calcpr = NULL;

	int pcount, xsize, ysize;
	double ymin, ymax;

	for (pcount = 0; (graphargs[pcount]); pcount++);
	rrd_clear_error();
	(void)xymon_rrd_graph(pcount, graphargs, &calcpr, &xsize, &ysize, &ymin, &ymax);

	return 0;
}
