/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HOBBITRRD_H__
#define __HOBBITRRD_H__

#include <time.h>
#include "rbtr.h"

/* This is for mapping a service -> an RRD file */
typedef struct {
   char *bbsvcname;
   char *hobbitrrdname;
} hobbitrrd_t;

/* This is for displaying an RRD file. */
typedef struct {
   char *hobbitrrdname;
   char *hobbitpartname;
   int  maxgraphs;
} hobbitgraph_t;

typedef enum {
	HG_WITHOUT_STALE_RRDS, HG_WITH_STALE_RRDS
} hg_stale_rrds_t;

typedef enum {
	HG_PLAIN_LINK, HG_META_LINK
} hg_link_t;

typedef struct rrdtpldata_t {
	char *template;
	RbtHandle dsnames;	/* Tree of tplnames_t records */
} rrdtpldata_t;
typedef struct rrdtplnames_t {
	char *dsnam;
	int idx;
} rrdtplnames_t;


extern hobbitrrd_t *hobbitrrds;
extern hobbitgraph_t *hobbitgraphs;

extern hobbitrrd_t *find_hobbit_rrd(char *service, char *flags);
extern hobbitgraph_t *find_hobbit_graph(char *rrdname);
extern char *hobbit_graph_data(char *hostname, char *dispname, char *service, int bgcolor,
		hobbitgraph_t *graphdef, int itemcount, 
		hg_stale_rrds_t nostale, hg_link_t wantmeta, int locatorbased,
		time_t starttime, time_t endtime);
extern rrdtpldata_t *setup_template(char *params[]);

#endif

