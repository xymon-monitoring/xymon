/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc-trends.c,v 1.38 2004-10-26 17:23:18 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>

#include "bbgen.h"
#include "util.h"
#include "larrdgen.h"
#include "debug.h"
#include "sendmsg.h"
#include "savelog.h"

char    *larrdcol = "larrd";
int 	enable_larrdgen = 0;
int 	larrd_update_interval = 300; /* Update LARRD pages every N seconds */
int     log_nohost_rrds = 0;

rrdlayout_t rrdnames[] = {
	{ "la",      	NULL,        0 },
	{ "disk",    	"disk_part", 5 },
	{ "memory",  	NULL,        0 },
	{ "tcp",     	NULL,        0 },
	{ "citrix",  	NULL,        0 },
	{ "users",   	NULL,        0 },
	{ "vmstat",  	NULL,        0 },
	{ "netstat", 	NULL,        0 },
	{ "iostat",  	NULL,        0 },
	{ "ntpstat", 	NULL,        0 },
	{ "vmio",    	NULL,        0 },
	{ "temperature",NULL,        0 },
	{ "apache",	NULL,        0 },
	{ "bind",	NULL,        0 },
	{ "sendmail",	NULL,        0 },
	{ "nmailq",	NULL,        0 },
	{ "socks",	NULL,        0 },
	{ "imap2",	NULL,        0 },
	{ "bbtest",	NULL,        0 },
	{ "bbproxy",	NULL,        0 },
	{ "bbgen",	NULL,        0 },
	{ NULL,      	NULL,        0 }
};


static char *rrdlink_url(char *hostname, char *dispname, rrd_t *rrd, int larrd043)
{
	static char *rrdurl = NULL;
	static int rrdurlsize = 0;
	char *svcurl;
	int svcurllen, rrdparturlsize;
	const char *linkfmt = "<br><A HREF=\"%s\"><IMG BORDER=0 SRC=\"%s&amp;graph=hourly\" ALT=\"larrd is accumulating %s\"></A>\n";

	dprintf("rrdlink_url: host %s, rrd %s (partname:%s, maxgraphs:%d, count=%d), larrd043=%d\n", 
		hostname, 
		rrd->rrdname->name, textornull(rrd->rrdname->partname), rrd->rrdname->maxgraphs, rrd->count, 
		larrd043);

	svcurllen = 2048                        + 
		    strlen(getenv("CGIBINURL")) + 
		    strlen(hostname)            + 
		    strlen(rrd->rrdname->name)  + 
		    (dispname ? strlen(urlencode(dispname)) : 0);
	svcurl = (char *) malloc(svcurllen);

	rrdparturlsize = 2048 +
			 strlen(linkfmt)    +
			 2*svcurllen        +
			 strlen(rrd->rrdname->name);

	if (rrdurl == NULL) {
		rrdurlsize = rrdparturlsize;
		rrdurl = (char *) malloc(rrdurlsize);
	}
	*rrdurl = '\0';

	if (larrd043 && rrd->rrdname->partname) {
		char *rrdparturl;
		int first = 0;

		rrdparturl = (char *) malloc(rrdparturlsize);
		do {
			int last = (first-1)+rrd->rrdname->maxgraphs;

			if (last > rrd->count) last = rrd->count;
			sprintf(svcurl, "%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;%s=%d..%d", 
				getenv("CGIBINURL"), hostname, rrd->rrdname->name,
				rrd->rrdname->partname, first, last);
			if (dispname) {
				strcat(svcurl, "&amp;disp=");
				strcat(svcurl, urlencode(dispname));
			}
			sprintf(rrdparturl, linkfmt, svcurl, svcurl, rrd->rrdname->name);
			if ((strlen(rrdparturl) + strlen(rrdurl) + 1) >= rrdurlsize) {
				rrdurlsize += (4096 + (rrd->count - last)*rrdparturlsize);
				rrdurl = (char *) realloc(rrdurl, rrdurlsize);
			}
			strcat(rrdurl, rrdparturl);
			first = last+1;
		} while (first < rrd->count);
		free(rrdparturl);
	}
	else {
		sprintf(svcurl, "%s/larrd-grapher.cgi?host=%s&amp;service=%s", 
			getenv("CGIBINURL"), hostname, rrd->rrdname->name);
		if (dispname) {
			strcat(svcurl, "&amp;disp=");
			strcat(svcurl, urlencode(dispname));
		}
		sprintf(rrdurl, linkfmt, svcurl, svcurl, rrd->rrdname->name);
	}

	dprintf("URLtext: %s\n", rrdurl);

	free(svcurl);
	return rrdurl;
}

static char *rrdlink_text(host_t *host, rrd_t *rrd, int larrd043)
{
	static char *rrdlink = NULL;
	static int rrdlinksize = 0;
	char *graphdef, *p;

	dprintf("rrdlink_text: host %s, rrd %s, larrd043=%d\n", host->hostname, rrd->rrdname->name, larrd043);

	/* If no larrdgraphs definition, include all with default links */
	if (host->larrdgraphs == NULL) {
		dprintf("rrdlink_text: Standard URL (no larrdgraphs)\n");
		return rrdlink_url(host->hostname, host->displayname, rrd, larrd043);
	}

	/* Find this rrd definition in the larrdgraphs */
	graphdef = strstr(host->larrdgraphs, rrd->rrdname->name);

	/* If not found ... */
	if (graphdef == NULL) {
		dprintf("rrdlink_text: NULL graphdef\n");

		/* Do we include all by default ? */
		if (*(host->larrdgraphs) == '*') {
			dprintf("rrdlink_text: Default URL included\n");

			/* Yes, return default link for this RRD */
			return rrdlink_url(host->hostname, host->displayname, rrd, larrd043);
		}
		else {
			dprintf("rrdlink_text: Default URL NOT included\n");
			/* No, return empty string */
			return "";
		}
	}

	/* We now know that larrdgraphs explicitly define what to do with this RRD */

	/* Does he want to explicitly exclude this RRD ? */
	if ((graphdef > host->larrdgraphs) && (*(graphdef-1) == '!')) {
		dprintf("rrdlink_text: This graph is explicitly excluded\n");
		return "";
	}

	/* It must be included. */
	if (rrdlink == NULL) {
		rrdlinksize = 4096;
		rrdlink = (char *)malloc(rrdlinksize);
	}
	*rrdlink = '\0';

	p = graphdef + strlen(rrd->rrdname->name);
	if (*p == ':') {
		/* There is an explicit list of graphs to add for this RRD. */
		char savechar;
		char *enddef;
		rrd_t *myrrd;
		char *partlink;

		myrrd = (rrd_t *) malloc(sizeof(rrd_t));
		myrrd->rrdname = (rrdlayout_t *) malloc(sizeof(rrdlayout_t));

		/* First, null-terminate this graph definition so we only look at the active RRD */
		enddef = strchr(graphdef, ',');
		if (enddef) *enddef = '\0';

		graphdef = (p+1);
		do {
			p = strchr(graphdef, '|');			/* Ends at '|' ? */
			if (p == NULL) p = graphdef + strlen(graphdef);	/* Ends at end of string */
			savechar = *p; *p = '\0'; 

			myrrd->rrdname->name = graphdef;
			myrrd->rrdname->partname = NULL;
			myrrd->rrdname->maxgraphs = 999;
			myrrd->count = 1;
			myrrd->next = NULL;
			partlink = rrdlink_url(host->hostname, host->displayname, myrrd, larrd043);
			if ((strlen(rrdlink) + strlen(partlink) + 1) >= rrdlinksize) {
				rrdlinksize += strlen(partlink) + 4096;
				rrdlink = (char *)realloc(rrdlink, rrdlinksize);
			}
			strcat(rrdlink, partlink);
			*p = savechar;

			graphdef = p;
			if (*graphdef != '\0') graphdef++;

		} while (*graphdef);

		if (enddef) *enddef = ',';
		free(myrrd->rrdname);
		free(myrrd);

		return rrdlink;
	}
	else {
		/* It is included with the default graph */
		return rrdlink_url(host->hostname, host->displayname, rrd, larrd043);
	}

	return "";
}


int generate_larrd(char *rrddirname, char *larrdcolumn, int larrd043, int bbgend)
{
	DIR *rrddir;
	struct dirent *d;
	char fn[MAX_PATH];
	hostlist_t *hostwalk;
	rrd_t *rwalk;
	int i;
	char *allrrdlinks;
	unsigned int allrrdlinksize;

	dprintf("generate_larrd(rrddirname=%s, larrcolumn=%s, larrd043=%d\n",
		 rrddirname, larrdcolumn, larrd043);

	if (!run_columngen("larrd", larrd_update_interval, enable_larrdgen)) {
		dprintf("Dropping larrd updates, larrd_update_interval=%d, enable_larrdgen=%d\n",
			larrd_update_interval, enable_larrdgen);
		return 1;
	}

	allrrdlinksize = 16384;
	allrrdlinks = (char *) malloc(allrrdlinksize);

	/*
	 * General idea: Scan the RRD directory for all RRD files, and 
	 * pick up which RRD's are present for each host.
	 * Since there are only a limited set of possible RRD links to
	 * generate, this does not take up a huge hunk of memory.
	 * Then, loop over the list of hosts, and generate a log
	 * file and an html file for the larrd column.
	 */

	chdir(rrddirname);
	rrddir = opendir(rrddirname);
	if (!rrddir) {
		errprintf("Cannot access RRD directory\n");
		return 1;
	}

	while ((d = readdir(rrddir))) {
		strcpy(fn, d->d_name);

		if ((strlen(fn) > 4) && (strcmp(fn+strlen(fn)-4, ".rrd") == 0) && (fn[0] != '.')) {
			char *p, *rrdname;
			rrdlayout_t *r = NULL;
			int found, hostfound;
			int i;

			dprintf("Got RRD %s\n", fn);

			/* Logfiles use ',' instead of '.' in FQDN hostnames */
			for (p=fn; *p; p++) {
				if (*p == ',') *p = '.';
			}

			/* Is this a known host? */
			hostwalk = hosthead; found = hostfound = 0;
			while (hostwalk && (!found)) {
				if (strncmp(hostwalk->hostentry->hostname, fn, strlen(hostwalk->hostentry->hostname)) == 0) {

					p = fn + strlen(hostwalk->hostentry->hostname);
					hostfound = ( (*p == '.') || (*p = ',') );

					/* First part of filename matches.
					 * Now check that there is a valid RRD id next -
					 * if not, then we may have hit a partial hostname 
					 */

					rrdname = fn + strlen(hostwalk->hostentry->hostname) + 1;
					p = strchr(rrdname, '.');
					if (p) *p = '\0';

					for (i=0; (rrdnames[i].name && (strcmp(rrdnames[i].name, rrdname) != 0)); i++) ;
					if (rrdnames[i].name) {
						found = 1;
						r = &rrdnames[i];
					}
				}

				if (!found) {
					hostwalk = hostwalk->next;
				}
			}

			if (found) {
				/* hostwalk now points to the host owning this RRD */
				for (rwalk = hostwalk->hostentry->rrds; (rwalk && (rwalk->rrdname != r)); rwalk = rwalk->next) ;
				if (rwalk == NULL) {
					rrd_t *newrrd = (rrd_t *) malloc(sizeof(rrd_t));

					newrrd->rrdname = r;
					newrrd->count = 1;
					newrrd->next = hostwalk->hostentry->rrds;
					hostwalk->hostentry->rrds = rwalk = newrrd;
					dprintf("larrd: New rrd for host:%s, rrd:%s\n",
						hostwalk->hostentry->hostname, r->name);
				}
				else {
					rwalk->count++;

					dprintf("larrd: Extra RRD for host %s, rrd %s   count:%d\n", 
						hostwalk->hostentry->hostname, 
						rwalk->rrdname->name, rwalk->count);
				}
			}

			if (!hostfound && log_nohost_rrds) {
				/* This rrd file has no matching host. */
				errprintf("No host record for rrd %s\n", d->d_name);
			}
		}
	}

	chdir(getenv("BBLOGS"));

	if (bbgend) combo_start();

	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
		char *rrdlink;
		int i;

		sprintf(allrrdlinks, "status %s.%s green %s - larrd is accumulating <center><BR>\n",
			commafy(hostwalk->hostentry->hostname), larrdcol, timestamp);

		for (i=0; rrdnames[i].name; i++) {
			for (rwalk = hostwalk->hostentry->rrds; (rwalk && (rwalk->rrdname->name != rrdnames[i].name)); rwalk = rwalk->next) ;
			if (rwalk) {
				rrdlink = rrdlink_text(hostwalk->hostentry, rwalk, larrd043);
				if ((strlen(allrrdlinks) + strlen(rrdlink)) >= allrrdlinksize) {
					allrrdlinksize += 4096;
					allrrdlinks = (char *) realloc(allrrdlinks, allrrdlinksize);
				}
				strcat(allrrdlinks, rrdlink);
			}
		}

		if (strlen(allrrdlinks) > 0) do_savelog(hostwalk->hostentry->hostname, hostwalk->hostentry->ip, 
							larrdcol, allrrdlinks, bbgend);
	}

	if (bbgend) combo_end();

	closedir(rrddir);
	free(allrrdlinks);
	return 0;
}

