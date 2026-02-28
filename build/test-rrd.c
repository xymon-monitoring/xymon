#include <stdio.h>

#include <rrd.h>
#include "../lib/xymonrrd.h"

#ifndef RRD_CONST_ARGS
#error "RRD_CONST_ARGS is not defined. Run configure or define RRD_CONST_ARGS (0 or 1)."
#endif

int main(int argc, char *argv[])
{
	xymon_rrd_argv_item_t rrdargs[] = {
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
	char **calcpr=NULL;

	int pcount, result, xsize, ysize;
	double ymin, ymax;

	(void)argc; (void)argv;
	for (pcount = 0; (rrdargs[pcount]); pcount++);
	rrd_clear_error();
	result = xymon_rrd_graph(pcount, rrdargs, &calcpr, &xsize, &ysize, NULL, &ymin, &ymax);
	(void)result;

	return 0;
}
