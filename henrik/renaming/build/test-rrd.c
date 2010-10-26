#include <stdio.h>

#include <rrd.h>

int main(int argc, char *argv[])
{
	char *rrdargs[] = {
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
#ifdef RRDTOOL12
	result = rrd_graph(pcount, rrdargs, &calcpr, &xsize, &ysize, NULL, &ymin, &ymax);
#else
	result = rrd_graph(pcount, rrdargs, &calcpr, &xsize, &ysize);
#endif

	return 0;
}

