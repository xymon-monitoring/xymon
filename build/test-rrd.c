#include <stdio.h>

#include <rrd.h>
#include "../lib/rrd_api_compat.h"

int main(void)
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

	for (pcount = 0; (rrdargs[pcount]); pcount++);
	rrd_clear_error();
	result = xymon_rrd_graph(pcount, rrdargs, &calcpr, &xsize, &ysize, NULL, &ymin, &ymax);
	(void)result;

	return 0;
}

