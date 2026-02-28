/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __XYMONRRD_H__
#define __XYMONRRD_H__

#include <time.h>

/* This is for mapping a service -> an RRD file */
typedef struct {
   char *svcname;
   char *xymonrrdname;
} xymonrrd_t;

/* This is for displaying an RRD file. */
typedef struct {
   char *xymonrrdname;
   char *xymonpartname;
   int  maxgraphs;
} xymongraph_t;

typedef enum {
	HG_WITHOUT_STALE_RRDS, HG_WITH_STALE_RRDS
} hg_stale_rrds_t;

typedef enum {
	HG_PLAIN_LINK, HG_META_LINK
} hg_link_t;

typedef struct rrdtpldata_t {
	char *template;
	void *dsnames;	/* Tree of tplnames_t records */
} rrdtpldata_t;
typedef struct rrdtplnames_t {
	char *dsnam;
	int idx;
} rrdtplnames_t;


extern xymonrrd_t *xymonrrds;
extern xymongraph_t *xymongraphs;

extern xymonrrd_t *find_xymon_rrd(char *service, char *flags);
extern xymongraph_t *find_xymon_graph(char *rrdname);
extern char *xymon_graph_data(char *hostname, char *dispname, char *service, int bgcolor,
		xymongraph_t *graphdef, int itemcount, 
		hg_stale_rrds_t nostale, hg_link_t wantmeta, int locatorbased,
		time_t starttime, time_t endtime);
extern rrdtpldata_t *setup_template(char *params[]);

#ifdef RRD_CONST_ARGS

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
				  char ***calcpr, int *xsize, int *ysize,
				  void *prdata, double *ymin, double *ymax)
{
	return rrd_graph(argc, xymon_rrd_api_argv(argv), calcpr, xsize, ysize, prdata, ymin, ymax);
}

#endif

#endif
