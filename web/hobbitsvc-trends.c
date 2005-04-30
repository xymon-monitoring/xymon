/*----------------------------------------------------------------------------*/
/* Hobbit RRD graph overview generator.                                       */
/*                                                                            */
/* This is a standalone tool for generating data for the trends column.       */
/* All of the data stored in RRD files for a host end up as graphs. Some of   */
/* these are displayed together with the corresponding status display, but    */
/* others (e.g. from "data" messages) are not. This generates a "trends"      */
/* column that contains all of the graphs for a host.                         */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc-trends.c,v 1.65 2005-04-30 07:16:05 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>

#include "libbbgen.h"

typedef struct graph_t {
	larrdgraph_t *gdef;
	int count;
	struct graph_t *next;
} graph_t;

typedef struct larrd_dirstack_t {
	char *dirname;
	DIR *rrddir;
	struct larrd_dirstack_t *next;
} larrd_dirstack_t;
larrd_dirstack_t *dirs = NULL;

static larrd_dirstack_t *larrd_opendir(char *dirname)
{
	larrd_dirstack_t *newdir;
	DIR *d;

	d = opendir(dirname);
	if (d == NULL) return NULL;

	newdir = (larrd_dirstack_t *)malloc(sizeof(larrd_dirstack_t));
	newdir->dirname = strdup(dirname);
	newdir->rrddir = d;
	newdir->next = NULL;

	if (dirs == NULL) {
		dirs = newdir;
	}
	else {
		newdir->next = dirs;
		dirs = newdir;
	}

	return newdir;
}

static void larrd_closedir(void)
{
	larrd_dirstack_t *tmp = dirs;

	if (dirs && dirs->rrddir) {
		dirs = dirs->next;

		closedir(tmp->rrddir);
		xfree(tmp->dirname);
		xfree(tmp);
	}
}

static char *larrd_readdir(void)
{
	static char fname[PATH_MAX];
	struct dirent *d;
	struct stat st;

	if (dirs == NULL) return NULL;

	do {
		d = readdir(dirs->rrddir);
		if (d == NULL) {
			larrd_closedir();
		}
		else if (*(d->d_name) == '.') {
			d = NULL;
		}
		else {
			sprintf(fname, "%s/%s", dirs->dirname, d->d_name);
			if ((stat(fname, &st) == 0) && (S_ISDIR(st.st_mode))) {
				larrd_opendir(fname);
				d = NULL;
			}
		}
	} while (dirs && (d == NULL));

	if (d == NULL) return NULL;

	if (strncmp(fname, "./", 2) == 0) return (fname + 2); else return fname;
}


static char *rrdlink_text(namelist_t *host, graph_t *rrd, int larrd043, int hobbitd, int wantmeta)
{
	static char *rrdlink = NULL;
	static int rrdlinksize = 0;
	char *graphdef, *p;
	char *hostdisplayname, *hostlarrdgraphs;

	hostdisplayname = bbh_item(host, BBH_DISPLAYNAME);
	hostlarrdgraphs = bbh_item(host, BBH_LARRD);

	dprintf("rrdlink_text: host %s, rrd %s, larrd043=%d\n", host->bbhostname, rrd->gdef->larrdrrdname, larrd043);

	/* If no larrdgraphs definition, include all with default links */
	if (hostlarrdgraphs == NULL) {
		dprintf("rrdlink_text: Standard URL (no larrdgraphs)\n");
		return larrd_graph_data(host->bbhostname, hostdisplayname, NULL, rrd->gdef, rrd->count, larrd043, hobbitd, wantmeta);
	}

	/* Find this rrd definition in the larrdgraphs */
	graphdef = strstr(hostlarrdgraphs, rrd->gdef->larrdrrdname);

	/* If not found ... */
	if (graphdef == NULL) {
		dprintf("rrdlink_text: NULL graphdef\n");

		/* Do we include all by default ? */
		if (*(hostlarrdgraphs) == '*') {
			dprintf("rrdlink_text: Default URL included\n");

			/* Yes, return default link for this RRD */
			return larrd_graph_data(host->bbhostname, hostdisplayname, NULL, rrd->gdef, rrd->count, larrd043, hobbitd, wantmeta);
		}
		else {
			dprintf("rrdlink_text: Default URL NOT included\n");
			/* No, return empty string */
			return "";
		}
	}

	/* We now know that larrdgraphs explicitly define what to do with this RRD */

	/* Does he want to explicitly exclude this RRD ? */
	if ((graphdef > hostlarrdgraphs) && (*(graphdef-1) == '!')) {
		dprintf("rrdlink_text: This graph is explicitly excluded\n");
		return "";
	}

	/* It must be included. */
	if (rrdlink == NULL) {
		rrdlinksize = 4096;
		rrdlink = (char *)malloc(rrdlinksize);
	}

	*rrdlink = '\0';

	p = graphdef + strlen(rrd->gdef->larrdrrdname);
	if (*p == ':') {
		/* There is an explicit list of graphs to add for this RRD. */
		char savechar;
		char *enddef;
		graph_t *myrrd;
		char *partlink;

		myrrd = (graph_t *) malloc(sizeof(graph_t));
		myrrd->gdef = (larrdgraph_t *) calloc(1, sizeof(larrdgraph_t));

		/* First, null-terminate this graph definition so we only look at the active RRD */
		enddef = strchr(graphdef, ',');
		if (enddef) *enddef = '\0';

		graphdef = (p+1);
		do {
			p = strchr(graphdef, '|');			/* Ends at '|' ? */
			if (p == NULL) p = graphdef + strlen(graphdef);	/* Ends at end of string */
			savechar = *p; *p = '\0'; 

			myrrd->gdef->larrdrrdname = graphdef;
			myrrd->gdef->larrdpartname = NULL;
			myrrd->gdef->maxgraphs = 0;
			myrrd->count = rrd->count;
			myrrd->next = NULL;
			partlink = larrd_graph_data(host->bbhostname, hostdisplayname, NULL, myrrd->gdef, myrrd->count, larrd043, hobbitd, wantmeta);
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
		xfree(myrrd->gdef);
		xfree(myrrd);

		return rrdlink;
	}
	else {
		/* It is included with the default graph */
		return larrd_graph_data(host->bbhostname, hostdisplayname, NULL, rrd->gdef, rrd->count, larrd043, hobbitd, wantmeta);
	}

	return "";
}


char *generate_trends(char *hostname)
{
	namelist_t *myhost;
	char hostrrddir[PATH_MAX];
	char *fn;
	int anyrrds = 0;
	larrdgraph_t *graph;
	graph_t *rwalk;
	char *allrrdlinks = NULL, *allrrdlinksend;
	unsigned int allrrdlinksize = 0;

	myhost = hostinfo(hostname);
	if (!myhost) return NULL;

	sprintf(hostrrddir, "%s/%s", xgetenv("BBRRDS"), hostname);
	chdir(hostrrddir);
	larrd_opendir(".");

	while ((fn = larrd_readdir())) {
		/* Check if the filename ends in ".rrd", and we know how to handle this RRD */
		if ((strlen(fn) <= 4) || (strcmp(fn+strlen(fn)-4, ".rrd") != 0)) continue;
		graph = find_larrd_graph(fn); if (!graph) continue;

		dprintf("Got RRD %s\n", fn);
		anyrrds++;

		for (rwalk = (graph_t *)myhost->data; (rwalk && (rwalk->gdef != graph)); rwalk = rwalk->next) ;
		if (rwalk == NULL) {
			graph_t *newrrd = (graph_t *) malloc(sizeof(graph_t));

			newrrd->gdef = graph;
			newrrd->count = 1;
			newrrd->next = (graph_t *)myhost->data;
			myhost->data = (void *)newrrd;
			rwalk = newrrd;
			dprintf("larrd: New rrd for host:%s, rrd:%s\n", hostname, graph->larrdrrdname);
		}
		else {
			rwalk->count++;

			dprintf("larrd: Extra RRD for host %s, rrd %s   count:%d\n", 
				hostname, 
				rwalk->gdef->larrdrrdname, rwalk->count);
		}
	}
	larrd_closedir();

	if (!anyrrds) return NULL;

	allrrdlinksize = 16384;
	allrrdlinks = (char *) malloc(allrrdlinksize);
	*allrrdlinks = '\0';
	allrrdlinksend = allrrdlinks;

	graph = larrdgraphs;
	while (graph->larrdrrdname) {
		for (rwalk = (graph_t *)myhost->data; (rwalk && (rwalk->gdef->larrdrrdname != graph->larrdrrdname)); rwalk = rwalk->next) ;
		if (rwalk) {
			int buflen;
			char *onelink;

			buflen = (allrrdlinksend - allrrdlinks);
			onelink = rrdlink_text(myhost, rwalk, 0, 1, 0);
			if ((buflen + strlen(onelink)) >= allrrdlinksize) {
				allrrdlinksize += (strlen(onelink) + 4096);
				allrrdlinks = (char *) realloc(allrrdlinks, allrrdlinksize);
				allrrdlinksend = allrrdlinks + buflen;
			}
			allrrdlinksend += sprintf(allrrdlinksend, "%s", onelink);
		}

		graph++;
	}

	return allrrdlinks;
}

