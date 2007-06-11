/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for working with RRD graphs.                          */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitrrd.c,v 1.44 2007-06-11 14:39:09 henrik Exp $";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "libbbgen.h"
#include "version.h"

/* This is for mapping a status-name -> RRD file */
hobbitrrd_t *hobbitrrds = NULL;
RbtHandle hobbitrrdtree;

/* This is the information needed to generate links on the trends column page  */
hobbitgraph_t *hobbitgraphs = NULL;

static const char *hobbitlinkfmt = "<table summary=\"%s Graph\"><tr><td><A HREF=\"%s&amp;action=menu\"><IMG BORDER=0 SRC=\"%s&amp;graph=hourly&amp;action=view\" ALT=\"hobbit graph %s\"></A></td><td> <td align=\"left\" valign=\"top\"> <a href=\"%s&amp;graph=custom&amp;action=selzoom\"> <img src=\"%s/zoom.gif\" border=0 alt=\"Zoom graph\" style='padding: 3px'> </a> </td></tr></table>\n";

static const char *metafmt = "<RRDGraph>\n  <GraphType>%s</GraphType>\n  <GraphLink><![CDATA[%s]]></GraphLink>\n  <GraphImage><![CDATA[%s&amp;graph=hourly]]></GraphImage>\n</RRDGraph>\n";


/*
 * Define the mapping between BB columns and RRD graphs.
 * Normally they are identical, but some RRD's use different names.
 */
static void rrd_setup(void)
{
	static int setup_done = 0;
	char *lenv, *ldef, *p, *tcptests, *services;
	int count;
	hobbitrrd_t *lrec;
	hobbitgraph_t *grec;

	/* Do nothing if we have been called within the past 5 minutes */
	if ((setup_done + 300) >= getcurrenttime(NULL)) return;


	/* 
	 * Must free any old data first.
	 * NB: These lists are NOT null-terminated ! 
	 *     Stop when bbsvcname becomes a NULL.
	 */
	lrec = hobbitrrds;
	while (lrec && lrec->bbsvcname) {
		if (lrec->hobbitrrdname != lrec->bbsvcname) xfree(lrec->hobbitrrdname);
		xfree(lrec->bbsvcname);
		lrec++;
	}
	if (hobbitrrds) {
		xfree(hobbitrrds);
		rbtDelete(hobbitrrdtree);
	}

	grec = hobbitgraphs;
	while (grec && grec->hobbitrrdname) {
		if (grec->hobbitpartname) xfree(grec->hobbitpartname);
		xfree(grec->hobbitrrdname);
		grec++;
	}
	if (hobbitgraphs) xfree(hobbitgraphs);


	/* Get the tcp services, and count how many there are */
	services = strdup(init_tcp_services());
	tcptests = strdup(services);
	count = 0; p = strtok(tcptests, " "); while (p) { count++; p = strtok(NULL, " "); }
	strcpy(tcptests, services);

	/* Setup the hobbitrrds table, mapping test-names to RRD files */
	lenv = (char *)malloc(strlen(xgetenv("TEST2RRD")) + strlen(tcptests) + count*strlen(",=tcp") + 1);
	strcpy(lenv, xgetenv("TEST2RRD")); 
	p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0';	/* Drop a trailing comma */
	p = strtok(tcptests, " "); while (p) { sprintf(lenv+strlen(lenv), ",%s=tcp", p); p = strtok(NULL, " "); }
	xfree(tcptests);
	xfree(services);

	count = 0; p = lenv; do { count++; p = strchr(p+1, ','); } while (p);
	hobbitrrds = (hobbitrrd_t *)calloc(sizeof(hobbitrrd_t), (count+1));

	hobbitrrdtree = rbtNew(name_compare);
	lrec = hobbitrrds; ldef = strtok(lenv, ",");
	while (ldef) {
		p = strchr(ldef, '=');
		if (p) {
			*p = '\0'; 
			lrec->bbsvcname = strdup(ldef);
			lrec->hobbitrrdname = strdup(p+1);
		}
		else {
			lrec->bbsvcname = lrec->hobbitrrdname = strdup(ldef);
		}
		rbtInsert(hobbitrrdtree, lrec->bbsvcname, lrec);

		ldef = strtok(NULL, ",");
		lrec++;
	}
	xfree(lenv);

	/* Setup the hobbitgraphs table, describing how to make graphs from an RRD */
	lenv = strdup(xgetenv("GRAPHS"));
	p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0';	/* Drop a trailing comma */
	count = 0; p = lenv; do { count++; p = strchr(p+1, ','); } while (p);
	hobbitgraphs = (hobbitgraph_t *)calloc(sizeof(hobbitgraph_t), (count+1));

	grec = hobbitgraphs; ldef = strtok(lenv, ",");
	while (ldef) {
		p = strchr(ldef, ':');
		if (p) {
			*p = '\0'; 
			grec->hobbitrrdname = strdup(ldef);
			grec->hobbitpartname = strdup(p+1);
			p = strchr(grec->hobbitpartname, ':');
			if (p) {
				*p = '\0';
				grec->maxgraphs = atoi(p+1);
				if (strlen(grec->hobbitpartname) == 0) {
					xfree(grec->hobbitpartname);
					grec->hobbitpartname = NULL;
				}
			}
		}
		else {
			grec->hobbitrrdname = strdup(ldef);
		}

		ldef = strtok(NULL, ",");
		grec++;
	}
	xfree(lenv);

	setup_done = getcurrenttime(NULL);
}


hobbitrrd_t *find_hobbit_rrd(char *service, char *flags)
{
	/* Lookup an entry in the hobbitrrds table */
	RbtHandle handle;

	rrd_setup();

	if (flags && (strchr(flags, 'R') != NULL)) {
		/* Dont do RRD's for reverse tests, since they have no data */
		return NULL;
	}

	handle = rbtFind(hobbitrrdtree, service);
	if (handle == rbtEnd(hobbitrrdtree)) 
		return NULL;
	else {
		void *k1, *k2;
		rbtKeyValue(hobbitrrdtree, handle, &k1, &k2);
		return (hobbitrrd_t *)k2;
	}
}

hobbitgraph_t *find_hobbit_graph(char *rrdname)
{
	/* Lookup an entry in the hobbitgraphs table */
	hobbitgraph_t *grec;
	int found = 0;
	char *dchar;

	rrd_setup();
	grec = hobbitgraphs; 
	while (!found && (grec->hobbitrrdname != NULL)) {
		found = (strncmp(grec->hobbitrrdname, rrdname, strlen(grec->hobbitrrdname)) == 0);
		if (found) {
			/* Check that it's not a partial match, e.g. "ftp" matches "ftps" */
			dchar = rrdname + strlen(grec->hobbitrrdname);
			if ( (*dchar != '.') && (*dchar != ',') && (*dchar != '\0') ) found = 0;
		}

		if (!found) grec++;
	}

	return (found ? grec : NULL);
}


static char *hobbit_graph_text(char *hostname, char *dispname, char *service, int bgcolor,
			      hobbitgraph_t *graphdef, int itemcount, hg_stale_rrds_t nostale, const char *fmt,
			      int locatorbased, time_t starttime, time_t endtime)
{
	static char *rrdurl = NULL;
	static int rrdurlsize = 0;
	static int gwidth = 0, gheight = 0;
	char *svcurl;
	int svcurllen, rrdparturlsize;
	char rrdservicename[100];
	char *cgiurl = xgetenv("CGIBINURL");

	MEMDEFINE(rrdservicename);

	if (locatorbased) {
		char *qres = locator_query(hostname, ST_RRD, &cgiurl);
		if (!qres) {
			errprintf("Cannot find RRD files for host %s\n", hostname);
			return "";
		}
	}

	if (!gwidth) {
		gwidth = atoi(xgetenv("RRDWIDTH"));
		gheight = atoi(xgetenv("RRDHEIGHT"));
	}

	dbgprintf("rrdlink_url: host %s, rrd %s (partname:%s, maxgraphs:%d, count=%d)\n", 
		hostname, 
		graphdef->hobbitrrdname, textornull(graphdef->hobbitpartname), graphdef->maxgraphs, itemcount);

	if ((service != NULL) && (strcmp(graphdef->hobbitrrdname, "tcp") == 0)) {
		sprintf(rrdservicename, "tcp:%s", service);
	}
	else if ((service != NULL) && (strcmp(graphdef->hobbitrrdname, "ncv") == 0)) {
		sprintf(rrdservicename, "ncv:%s", service);
	}
	else {
		strcpy(rrdservicename, graphdef->hobbitrrdname);
	}

	svcurllen = 2048                        + 
		    strlen(cgiurl)              + 
		    strlen(hostname)            + 
		    strlen(rrdservicename)  + 
		    strlen(urlencode(dispname ? dispname : hostname));
	svcurl = (char *) malloc(svcurllen);

	rrdparturlsize = 2048 +
			 strlen(fmt)        +
			 3*svcurllen        +
			 strlen(rrdservicename) +
			 strlen(xgetenv("BBSKIN"));

	if (rrdurl == NULL) {
		rrdurlsize = rrdparturlsize;
		rrdurl = (char *) malloc(rrdurlsize);
	}
	*rrdurl = '\0';

	{
		char *rrdparturl;
		int first = 1;
		int step;

		step = (graphdef->maxgraphs ? graphdef->maxgraphs : 5);
		if (itemcount) {
			int gcount = (itemcount / step); if ((gcount*step) != itemcount) gcount++;
			step = (itemcount / gcount);
		}

		rrdparturl = (char *) malloc(rrdparturlsize);
		do {
			if (itemcount > 0) {
				sprintf(svcurl, "%s/hobbitgraph.sh?host=%s&amp;service=%s&amp;graph_width=%d&amp;graph_height=%d&amp;first=%d&amp;count=%d", 
					cgiurl, hostname, rrdservicename, 
					gwidth, gheight,
					first, step);
			}
			else {
				sprintf(svcurl, "%s/hobbitgraph.sh?host=%s&amp;service=%s&amp;graph_width=%d&amp;graph_height=%d", 
					cgiurl, hostname, rrdservicename,
					gwidth, gheight);
			}

			strcat(svcurl, "&amp;disp=");
			strcat(svcurl, urlencode(dispname ? dispname : hostname));

			if (nostale == HG_WITHOUT_STALE_RRDS) strcat(svcurl, "&amp;nostale");
			if (bgcolor != -1) sprintf(svcurl+strlen(svcurl), "&amp;color=%s", colorname(bgcolor));
			sprintf(svcurl+strlen(svcurl), "&amp;graph_start=%d&amp;graph_end=%d", starttime, endtime);

			sprintf(rrdparturl, fmt, rrdservicename, svcurl, svcurl, rrdservicename, svcurl, xgetenv("BBSKIN"));
			if ((strlen(rrdparturl) + strlen(rrdurl) + 1) >= rrdurlsize) {
				rrdurlsize += (4096 + rrdparturlsize);
				rrdurl = (char *) realloc(rrdurl, rrdurlsize);
			}
			strcat(rrdurl, rrdparturl);
			first += step;
		} while (first <= itemcount);
		xfree(rrdparturl);
	}

	dbgprintf("URLtext: %s\n", rrdurl);

	xfree(svcurl);

	MEMUNDEFINE(rrdservicename);

	return rrdurl;
}

char *hobbit_graph_data(char *hostname, char *dispname, char *service, int bgcolor,
			hobbitgraph_t *graphdef, int itemcount,
			hg_stale_rrds_t nostale, hg_link_t wantmeta, int locatorbased,
			time_t starttime, time_t endtime)
{
	return hobbit_graph_text(hostname, dispname, 
				 service, bgcolor, graphdef, 
				 itemcount, nostale,
				 ((wantmeta == HG_META_LINK) ? metafmt : hobbitlinkfmt),
				 locatorbased, starttime, endtime);
}

