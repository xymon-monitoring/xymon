/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for working with LARRD graphs.                        */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitrrd.c,v 1.17 2005-01-20 22:02:23 henrik Exp $";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "libbbgen.h"
#include "version.h"

#include "bblarrd.h"

larrdrrd_t *larrdrrds = NULL;
larrdgraph_t *larrdgraphs = NULL;

/* This is for mapping a status-name -> RRD file */
static char *default_rrds =
        "cpu=la,disk,"						/* BB client status */
	"memory,"						/* bb-memory status */
	"conn=tcp,fping=tcp,"					/* bbgen bbtest-net status */
	"ftp=tcp,ftps=tcp,"					/* bbgen bbtest-net status */
	"ssh=tcp,ssh1=tcp,ssh2=tcp,"				/* bbgen bbtest-net status */
	"telnet=tcp,telnets=tcp,"				/* bbgen bbtest-net status */
	"smtp=tcp,smtps=tcp,"					/* bbgen bbtest-net status */
	"pop-2=tcp,pop2=tcp,"					/* bbgen bbtest-net status */
	"pop-3=tcp,pop3=tcp,"					/* bbgen bbtest-net status */
	"pop=tcp,pop3s=tcp,"					/* bbgen bbtest-net status */
	"imap=tcp,imap2=tcp,imap3=tcp,imap4=tcp,imaps=tcp,"	/* bbgen bbtest-net status */
	"nntp=tcp,nntps=tcp,"					/* bbgen bbtest-net status */
	"ldap=tcp,ldaps=tcp,"					/* bbgen bbtest-net status */
	"rsync=tcp,bbd=tcp,clamd=tcp,oratns=tcp,"		/* bbgen bbtest-net status */
	"qmtp=tcp,qmqp=tcp,"					/* bbgen bbtest-net status */
	"http=tcp,"						/* bbgen bbtest-net status */
	"apache,"						/* bbgen bbtest-net special apache BF data */
	"dns=tcp,dig=tcp,time=ntpstat,"				/* bbgen bbtest-net special tests status */
	"vmstat,iostat,netstat,"				/* LARRD standard bottom-feeders data */
	"temperature,bind,sendmail,nmailq,socks,"		/* LARRD non-standard bottom-feeders data */
	"bea,citrix,"						/* bbgen extra bottom-feeders data */
	"bbgen,bbtest,bbproxy,hobbitd"				/* bbgen report status */
	;

/* This is the information needed to generate links to larrd-grapher.cgi */
static char *default_graphs =
	"la,disk:disk_part:5,memory,users,"
	"vmstat,iostat,"
	"tcp.http,tcp,netstat,"
	"temperature,ntpstat,"
	"apache,bind,sendmail,nmailq,socks,"
	"bea,citrix,"
	"bbgen,bbtest,bbproxy,hobbitd,"
	;

static const char *linkfmt = "<br><A HREF=\"%s\"><IMG BORDER=0 SRC=\"%s&amp;graph=hourly\" ALT=\"larrd is accumulating %s\"></A>\n";
static const char *metafmt = "<RRDGraph>\n  <GraphLink><![CDATA[%s]]></GraphLink>\n  <GraphImage><![CDATA[%s&graph=hourly]]></GraphImage>\n</RRDGraph>\n";

/*
 * Define the mapping between BB columns and LARRD graphs.
 * Normally they are identical, but some RRD's use different names.
 */
static void larrd_setup(void)
{
	static int setup_done = 0;
	char *lenv, *ldef, *p;
	int count;
	larrdrrd_t *lrec;
	larrdgraph_t *grec;

	if (setup_done) return;


	/* Setup the larrdrrds table, mapping test-names to RRD files */
	getenv_default("LARRDS", default_rrds, NULL);
	lenv = strdup(xgetenv("LARRDS"));
	p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0';	/* Drop a trailing comma */
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
	getenv_default("GRAPHS", default_graphs, NULL);
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

	setup_done = 1;
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
	else {
		strcpy(rrdservicename, graphdef->larrdrrdname);
	}

	svcurllen = 2048                        + 
		    strlen(xgetenv("CGIBINURL")) + 
		    strlen(hostname)            + 
		    strlen(rrdservicename)  + 
		    (dispname ? strlen(urlencode(dispname)) : 0);
	svcurl = (char *) malloc(svcurllen);

	rrdparturlsize = 2048 +
			 strlen(fmt)        +
			 2*svcurllen        +
			 strlen(rrdservicename);

	if (rrdurl == NULL) {
		rrdurlsize = rrdparturlsize;
		rrdurl = (char *) malloc(rrdurlsize);
	}
	*rrdurl = '\0';

	if (hobbitd) {
		char *rrdparturl;
		int first = 1;
		int step = 5;

		if (itemcount > 0) {
			int gcount = (itemcount / 5); if ((gcount*5) != itemcount) gcount++;
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

			if (dispname) {
				strcat(svcurl, "&amp;disp=");
				strcat(svcurl, urlencode(dispname));
			}

			sprintf(rrdparturl, fmt, svcurl, svcurl, rrdservicename);
			if ((strlen(rrdparturl) + strlen(rrdurl) + 1) >= rrdurlsize) {
				rrdurlsize += (4096 + (itemcount - (first+step-1))*rrdparturlsize);
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
			if (dispname) {
				strcat(svcurl, "&amp;disp=");
				strcat(svcurl, urlencode(dispname));
			}
			sprintf(rrdparturl, fmt, svcurl, svcurl, rrdservicename);
			if ((strlen(rrdparturl) + strlen(rrdurl) + 1) >= rrdurlsize) {
				rrdurlsize += (4096 + (itemcount - last)*rrdparturlsize);
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
		if (dispname) {
			strcat(svcurl, "&amp;disp=");
			strcat(svcurl, urlencode(dispname));
		}
		sprintf(rrdurl, fmt, svcurl, svcurl, rrdservicename);
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
		return larrd_graph_text(hostname, dispname, service, graphdef, itemcount, 1, 0, metafmt);
	else
		return larrd_graph_text(hostname, dispname, service, graphdef, itemcount, larrd043, hobbitd, linkfmt);
}

