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
		hg_stale_rrds_t nostale, int locatorbased,
		time_t starttime, time_t endtime);
extern rrdtpldata_t *setup_template(char *params[]);

#endif

