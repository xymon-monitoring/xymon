/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for working with LARRD graphs.                        */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitrrd.c,v 1.31 2005-04-01 21:39:31 henrik Exp $";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "libbbgen.h"
#include "version.h"

#include "bblarrd.h"

/* This is for mapping a status-name -> RRD file */
larrdrrd_t *larrdrrds = NULL;

/* This is the information needed to generate links to larrd-grapher.cgi */
larrdgraph_t *larrdgraphs = NULL;

static const char *bblinkfmt = "<br><A HREF=\"%s\"><IMG BORDER=0 SRC=\"%s&amp;graph=hourly\" ALT=\"larrd is accumulating %s\"></A>\n";

static const char *hobbitlinkfmt = "<table summary=\"%s Graph\"><tr><td><A HREF=\"%s&amp;action=menu\"><IMG BORDER=0 SRC=\"%s&amp;graph=hourly&amp;action=view\" ALT=\"hobbit graph %s\"></A></td><td> <td align=\"left\" valign=\"top\"> <a href=\"%s&amp;graph=hourly&amp;action=selzoom\"> <img src=\"%s/zoom.gif\" border=0 alt=\"Zoom graph\" style='padding: 3px'> </a> </td></tr></table>\n";

static const char *metafmt = "<RRDGraph>\n  <GraphType>%s</GraphType>\n  <GraphLink><![CDATA[%s]]></GraphLink>\n  <GraphImage><![CDATA[%s&amp;graph=hourly]]></GraphImage>\n</RRDGraph>\n";

/*
 * Define the mapping between BB columns and LARRD graphs.
 * Normally they are identical, but some RRD's use different names.
 */
static void larrd_setup(void)
{
	static int setup_done = 0;
	char *lenv, *ldef, *p, *tcptests, *services;
	int count;
	larrdrrd_t *lrec;
	larrdgraph_t *grec;

	/* Do nothing if we have been called within the past 5 minutes */
	if ((setup_done + 300) >= time(NULL)) return;


	/* 
	 * Must free any old data first.
	 * NB: These lists are NOT null-terminated ! 
	 *     Stop when bbsvcname becomes a NULL.
	 */
	lrec = larrdrrds;
	while (lrec && lrec->bbsvcname) {
		if (lrec->larrdrrdname != lrec->bbsvcname) xfree(lrec->larrdrrdname);
		xfree(lrec->bbsvcname);
		lrec++;
	}
	if (larrdrrds) xfree(larrdrrds);

	grec = larrdgraphs;
	while (grec && grec->larrdrrdname) {
		if (grec->larrdpartname) xfree(grec->larrdpartname);
		xfree(grec->larrdrrdname);
		grec++;
	}
	if (larrdgraphs) xfree(larrdgraphs);


	/* Get the tcp services, and count how many there are */
	services = strdup(init_tcp_services());
	tcptests = strdup(services);
	count = 0; p = strtok(tcptests, " "); while (p) { count++; p = strtok(NULL, " "); }
	strcpy(tcptests, services);

	/* Setup the larrdrrds table, mapping test-names to RRD files */
	lenv = (char *)malloc(strlen(xgetenv("LARRDS")) + strlen(tcptests) + count*strlen(",=tcp") + 1);
	strcpy(lenv, xgetenv("LARRDS")); 
	p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0';	/* Drop a trailing comma */
	p = strtok(tcptests, " "); while (p) { sprintf(lenv+strlen(lenv), ",%s=tcp", p); p = strtok(NULL, " "); }
	xfree(tcptests);
	xfree(services);

	count = 0; p = lenv; do { count++; p = strchr(p+1, ','); } while (p);
	larrdrrds = (larrdrrd_t *)calloc(sizeof(larrdrrd_t), (count+1));

	lrec = larrdrrds; ldef = strtok(lenv, ",");
	while (ldef) {
		p = strchr(ldef, '=');
		if (p) {
			*p = '\0'; 
			lrec->bbsvcname = strdup(ldef);
			lrec->larrdrrdname = strdup(p+1);
		}
		else {
			lrec->bbsvcname = lrec->larrdrrdname = strdup(ldef);
		}

		ldef = strtok(NULL, ",");
		lrec++;
	}
	xfree(lenv);

	/* Setup the larrdgraphs table, describing how to make graphs from an RRD */
	lenv = strdup(xgetenv("GRAPHS"));
	p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0';	/* Drop a trailing comma */
	count = 0; p = lenv; do { count++; p = strchr(p+1, ','); } while (p);
	larrdgraphs = (larrdgraph_t *)calloc(sizeof(larrdgraph_t), (count+1));

	grec = larrdgraphs; ldef = strtok(lenv, ",");
	while (ldef) {
		p = strchr(ldef, ':');
		if (p) {
			*p = '\0'; 
			grec->larrdrrdname = strdup(ldef);
			grec->larrdpartname = strdup(p+1);
			p = strchr(grec->larrdpartname, ':');
			if (p) {
				*p = '\0';
				grec->maxgraphs = atoi(p+1);
				if (strlen(grec->larrdpartname) == 0) {
					xfree(grec->larrdpartname);
					grec->larrdpartname = NULL;
				}
			}
		}
		else {
			grec->larrdrrdname = strdup(ldef);
		}

		ldef = strtok(NULL, ",");
		grec++;
	}
	xfree(lenv);

	setup_done = time(NULL);
}


larrdrrd_t *find_larrd_rrd(char *service, char *flags)
{
	/* Lookup an entry in the larrdrrds table */
	larrdrrd_t *lrec;

	larrd_setup();

	if (flags && (strchr(flags, 'R') != NULL)) {
		/* Dont do LARRD for reverse tests, since they have no data */
		return NULL;
	}

	lrec = larrdrrds; while (lrec->bbsvcname && strcmp(lrec->bbsvcname, service)) lrec++;
	return (lrec->bbsvcname ? lrec : NULL);
}

larrdgraph_t *find_larrd_graph(char *rrdname)
{
	/* Lookup an entry in the larrdgraphs table */
	larrdgraph_t *grec;
	int found = 0;
	char *dchar;

	larrd_setup();
	grec = larrdgraphs; 
	while (!found && (grec->larrdrrdname != NULL)) {
		found = (strncmp(grec->larrdrrdname, rrdname, strlen(grec->larrdrrdname)) == 0);
		if (found) {
			/* Check that it's not a partial match, e.g. "ftp" matches "ftps" */
			dchar = rrdname + strlen(grec->larrdrrdname);
			if ( (*dchar != '.') && (*dchar != ',') && (*dchar != '\0') ) found = 0;
		}

		if (!found) grec++;
	}

	return (found ? grec : NULL);
}


static char *larrd_graph_text(char *hostname, char *dispname, char *service, 
			      larrdgraph_t *graphdef, int itemcount, int larrd043, int hobbitd,
			      const char *fmt)
{
	static char *rrdurl = NULL;
	static int rrdurlsize = 0;
	char *svcurl;
	int svcurllen, rrdparturlsize;
	char rrdservicename[100];

	MEMDEFINE(rrdservicename);

	dprintf("rrdlink_url: host %s, rrd %s (partname:%s, maxgraphs:%d, count=%d), larrd043=%d\n", 
		hostname, 
		graphdef->larrdrrdname, textornull(graphdef->larrdpartname), graphdef->maxgraphs, itemcount, 
		larrd043);

	if ((service != NULL) && (strcmp(graphdef->larrdrrdname, "tcp") == 0)) {
		sprintf(rrdservicename, "tcp:%s", service);
	}
	else if ((service != NULL) && (strcmp(graphdef->larrdrrdname, "ncv") == 0)) {
		sprintf(rrdservicename, "ncv:%s", service);
	}
	else {
		strcpy(rrdservicename, graphdef->larrdrrdname);
	}

	svcurllen = 2048                        + 
		    strlen(xgetenv("CGIBINURL")) + 
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

	if (hobbitd) {
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
				sprintf(svcurl, "%s/hobbitgraph.sh?host=%s&amp;service=%s&amp;first=%d&amp;count=%d", 
					xgetenv("CGIBINURL"), hostname, rrdservicename, first, step);
			}
			else {
				sprintf(svcurl, "%s/hobbitgraph.sh?host=%s&amp;service=%s", 
					xgetenv("CGIBINURL"), hostname, rrdservicename);
			}

			strcat(svcurl, "&amp;disp=");
			strcat(svcurl, urlencode(dispname ? dispname : hostname));

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
	else if (larrd043 && graphdef->larrdpartname && (itemcount > 0)) {
		char *rrdparturl;
		int first = 0;

		rrdparturl = (char *) malloc(rrdparturlsize);
		do {
			int last;
			
			last = (first-1)+graphdef->maxgraphs; if (last > itemcount) last = itemcount;

			sprintf(svcurl, "%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;%s=%d..%d", 
				xgetenv("CGIBINURL"), hostname, rrdservicename,
				graphdef->larrdpartname, first, last);
			strcat(svcurl, "&amp;disp=");
			strcat(svcurl, urlencode(dispname ? dispname : hostname));
			sprintf(rrdparturl, fmt, svcurl, svcurl, rrdservicename, svcurl, xgetenv("BBSKIN"));
			if ((strlen(rrdparturl) + strlen(rrdurl) + 1) >= rrdurlsize) {
				rrdurlsize += (4096 + rrdparturlsize);
				rrdurl = (char *) realloc(rrdurl, rrdurlsize);
			}
			strcat(rrdurl, rrdparturl);
			first = last+1;
		} while (first < itemcount);
		xfree(rrdparturl);
	}
	else {
		sprintf(svcurl, "%s/larrd-grapher.cgi?host=%s&amp;service=%s", 
			xgetenv("CGIBINURL"), hostname, rrdservicename);
		strcat(svcurl, "&amp;disp=");
		strcat(svcurl, urlencode(dispname ? dispname : hostname));
		sprintf(rrdurl, fmt, svcurl, svcurl, rrdservicename, svcurl, xgetenv("BBSKIN"));
	}

	dprintf("URLtext: %s\n", rrdurl);

	xfree(svcurl);

	MEMUNDEFINE(rrdservicename);

	return rrdurl;
}


char *larrd_graph_data(char *hostname, char *dispname, char *service, 
		      larrdgraph_t *graphdef, int itemcount, int larrd043, int hobbitd,
		      int wantmeta)
{
	if (wantmeta)
		return larrd_graph_text(hostname, dispname, service, graphdef, itemcount, 1, 1, metafmt);
	else if (hobbitd)
		return larrd_graph_text(hostname, dispname, service, graphdef, itemcount, larrd043, hobbitd, hobbitlinkfmt);
	else
		return larrd_graph_text(hostname, dispname, service, graphdef, itemcount, larrd043, hobbitd, bblinkfmt);
}

