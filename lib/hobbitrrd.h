/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBLARRD_H__
#define __BBLARRD_H__

/* This is for mapping a service -> an RRD file */
typedef struct {
   char *bbsvcname;
   char *larrdrrdname;
} larrdrrd_t;

/* This is for displaying an RRD file. */
typedef struct {
   char *larrdrrdname;
   char *larrdpartname;
   int  maxgraphs;
} larrdgraph_t;

extern larrdrrd_t *larrdrrds;
extern larrdgraph_t *larrdgraphs;

extern larrdrrd_t *find_larrd_rrd(char *service, char *flags);
extern larrdgraph_t *find_larrd_graph(char *rrdname);
extern char *larrd_graph_data(char *hostname, char *dispname, char *service, larrdgraph_t *graphdef, int itemcount, int larrd043, int bbgend, int wantmeta);

#endif

