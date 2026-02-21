#ifndef XYMON_RRD_COMPAT_H
#define XYMON_RRD_COMPAT_H

#include <stddef.h>
#include <time.h>
#include <rrd.h>

/*
 * RRDtool API compatibility:
 * - Some releases declare argv parameters as char **.
 * - Newer releases declare argv parameters as const char **.
 *
 * Usage:
 * - Declare argv vectors as xymon_rrd_argv_item_t[].
 * - Call the xymon_rrd_update/create/fetch/graph wrappers only.
 *
 * RRD_CONST_ARGS is detected by build/rrd.sh and propagated via RRDDEF.
 */
#ifndef RRD_CONST_ARGS
/*
 * Ad-hoc/manual builds that skip configure must opt in explicitly.
 */
#if defined(RRDTOOL19)
#define RRD_CONST_ARGS 1
#endif
#ifndef RRD_CONST_ARGS
#error "RRD_CONST_ARGS is not defined. Run configure or define RRDTOOL19 (or RRD_CONST_ARGS directly)."
#endif
#endif

#if RRD_CONST_ARGS
typedef const char *xymon_rrd_argv_item_t;
static inline const char **xymon_rrd_api_argv(xymon_rrd_argv_item_t *argv)
{
	return (const char **)argv;
}
#else
typedef char *xymon_rrd_argv_item_t;
static inline char **xymon_rrd_api_argv(xymon_rrd_argv_item_t *argv)
{
	return (char **)argv;
}
#endif

static inline int xymon_rrd_update(int argc, xymon_rrd_argv_item_t *argv)
{
	return rrd_update(argc, xymon_rrd_api_argv(argv));
}

static inline int xymon_rrd_create(int argc, xymon_rrd_argv_item_t *argv)
{
	return rrd_create(argc, xymon_rrd_api_argv(argv));
}

static inline int xymon_rrd_fetch(int argc, xymon_rrd_argv_item_t *argv,
				  time_t *start, time_t *end, unsigned long *step, unsigned long *dscount,
				  char ***dsnames, rrd_value_t **data)
{
	return rrd_fetch(argc, xymon_rrd_api_argv(argv), start, end, step, dscount, dsnames, data);
}

static inline int xymon_rrd_graph(int argc, xymon_rrd_argv_item_t *argv,
				  char ***calcpr, int *xsize, int *ysize, double *ymin, double *ymax)
{
	return rrd_graph(argc, xymon_rrd_api_argv(argv), calcpr, xsize, ysize, NULL, ymin, ymax);
}

#endif /* XYMON_RRD_COMPAT_H */
