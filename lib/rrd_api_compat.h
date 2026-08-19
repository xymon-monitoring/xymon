#ifndef __RRD_API_COMPAT_H__
#define __RRD_API_COMPAT_H__

#include <unistd.h>	/* optind/opterr: librrd parses these argv forms with getopt() */
#include <rrd.h>

/* A strictly-conforming <unistd.h> (e.g. plain -std=c99) keeps the getopt
 * globals to itself; librrd uses them regardless. */
extern int optind, opterr;

/*
 * RRDtool changed the argv parameter of its public API from "char **" to
 * "const char **". This header hides that difference behind a single argv
 * item type and a set of thin wrappers, so callers do not need their own
 * per-call casts or #ifdefs.
 *
 * RRD_CONST_ARGS (0 or 1) is determined by build/rrd.sh, which probes rrd.h.
 */

#ifndef RRD_CONST_ARGS
#error "RRD_CONST_ARGS is not defined. Run configure or define RRD_CONST_ARGS (0 or 1)."
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
	return argv;
}
#endif

/*
 * librrd parses these argv forms with getopt(): reset its globals and the
 * error state before every call; the state after a call is the caller's.
 * optind=0 is the GNU re-init - moot on RRDtool >= 1.7, whose private
 * optparse ignores the getopt globals. That version is the supported floor.
 */
static inline void xymon_rrd_call_setup(void)
{
	optind = opterr = 0;
	rrd_clear_error();
}

static inline int xymon_rrd_update(int argc, xymon_rrd_argv_item_t *argv)
{
	xymon_rrd_call_setup();
	return rrd_update(argc, xymon_rrd_api_argv(argv));
}

static inline int xymon_rrd_create(int argc, xymon_rrd_argv_item_t *argv)
{
	xymon_rrd_call_setup();
	return rrd_create(argc, xymon_rrd_api_argv(argv));
}

static inline int xymon_rrd_fetch(int argc, xymon_rrd_argv_item_t *argv,
				  time_t *start, time_t *end, unsigned long *step,
				  unsigned long *dscount, char ***dsnames, rrd_value_t **data)
{
	xymon_rrd_call_setup();
	return rrd_fetch(argc, xymon_rrd_api_argv(argv), start, end, step, dscount, dsnames, data);
}

static inline time_t xymon_rrd_last(int argc, xymon_rrd_argv_item_t *argv)
{
	xymon_rrd_call_setup();
	return rrd_last(argc, xymon_rrd_api_argv(argv));
}

static inline int xymon_rrd_graph(int argc, xymon_rrd_argv_item_t *argv,
				  char ***calcpr, int *xsize, int *ysize,
				  void *prdata, double *ymin, double *ymax)
{
	xymon_rrd_call_setup();
	return rrd_graph(argc, xymon_rrd_api_argv(argv), calcpr, xsize, ysize, prdata, ymin, ymax);
}

#endif
