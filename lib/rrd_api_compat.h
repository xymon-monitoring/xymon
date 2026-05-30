#ifndef __RRD_API_COMPAT_H__
#define __RRD_API_COMPAT_H__

#include <rrd.h>

/*
 * RRDtool changed the argv parameter of its public API from "char **" to
 * "const char **". This header hides that difference behind a single argv
 * item type and a set of thin wrappers, so callers do not need their own
 * per-call casts or #ifdefs.
 */

#ifdef RRDTOOL19
typedef const char *xymon_rrd_argv_item_t;
static const char **xymon_rrd_api_argv(xymon_rrd_argv_item_t *argv)
{
	return (const char **)argv;
}
#else
typedef char *xymon_rrd_argv_item_t;
static char **xymon_rrd_api_argv(xymon_rrd_argv_item_t *argv)
{
	return argv;
}
#endif

static int xymon_rrd_update(int argc, xymon_rrd_argv_item_t *argv)
{
	return rrd_update(argc, xymon_rrd_api_argv(argv));
}

static int xymon_rrd_create(int argc, xymon_rrd_argv_item_t *argv)
{
	return rrd_create(argc, xymon_rrd_api_argv(argv));
}

static int xymon_rrd_fetch(int argc, xymon_rrd_argv_item_t *argv,
				  time_t *start, time_t *end, unsigned long *step,
				  unsigned long *dscount, char ***dsnames, rrd_value_t **data)
{
	return rrd_fetch(argc, xymon_rrd_api_argv(argv), start, end, step, dscount, dsnames, data);
}

static int xymon_rrd_graph(int argc, xymon_rrd_argv_item_t *argv,
				  char ***calcpr, int *xsize, int *ysize,
				  void *prdata, double *ymin, double *ymax)
{
	return rrd_graph(argc, xymon_rrd_api_argv(argv), calcpr, xsize, ysize, prdata, ymin, ymax);
}

#endif
