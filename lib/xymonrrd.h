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
   int  maxinstancesperimage;
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
extern int xymon_gdef_maxinstancesperimage(char *name);
extern int xymon_gdef_store_allowed(char *fn);
extern int xymon_gdef_fileset_unknown(char *name);
extern int xymon_gdef_fileset_count(char *hostname, char *name);
extern int xymon_gdef_thresholds_off(char *name);
/* The staleness window is one fixed number - main's historic value.
 * Instances a graph's EXSTALEPATTERN matches are exempt: never stale. */
#define XYMON_STALE_WINDOW 86400
extern int xymon_gdef_stale_exempt(char *name, char *fn);

/* Consolidation-function bits for xymon_gdef_cfs_forfile() */
#define XYMON_CF_AVERAGE 1
#define XYMON_CF_MIN     2
#define XYMON_CF_MAX     4
#define XYMON_CF_LAST    8
extern int xymon_gdef_cfs_forfile(char *fn);
extern void xymon_gdef_meta_source(char *fn);
extern void rrd_destroy(void);
extern char *xymon_graph_data(char *hostname, char *dispname, char *service, int bgcolor,
		xymongraph_t *graphdef, int itemcount, 
		hg_stale_rrds_t nostale, hg_link_t wantmeta, int locatorbased,
		time_t starttime, time_t endtime);
extern rrdtpldata_t *setup_template(char *params[]);

#endif

