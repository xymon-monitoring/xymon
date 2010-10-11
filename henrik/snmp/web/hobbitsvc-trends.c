/*----------------------------------------------------------------------------*/
/* Hobbit RRD graph overview generator.                                       */
/*                                                                            */
/* This is a standalone tool for generating data for the trends column.       */
/* All of the data stored in RRD files for a host end up as graphs. Some of   */
/* these are displayed together with the corresponding status display, but    */
/* others (e.g. from "data" messages) are not. This generates a "trends"      */
/* column that contains all of the graphs for a host.                         */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

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
	hobbitgraph_t *gdef;
	int count;
	struct graph_t *next;
} graph_t;

typedef struct dirstack_t {
	char *dirname;
	DIR *rrddir;
	struct dirstack_t *next;
} dirstack_t;
dirstack_t *dirs = NULL;

static dirstack_t *stack_opendir(char *dirname)
{
	dirstack_t *newdir;
	DIR *d;

	d = opendir(dirname);
	if (d == NULL) return NULL;

	newdir = (dirstack_t *)malloc(sizeof(dirstack_t));
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

static void stack_closedir(void)
{
	dirstack_t *tmp = dirs;

	if (dirs && dirs->rrddir) {
		dirs = dirs->next;

		closedir(tmp->rrddir);
		xfree(tmp->dirname);
		xfree(tmp);
	}
}

static char *stack_readdir(void)
{
	static char fname[PATH_MAX];
	struct dirent *d;
	struct stat st;

	if (dirs == NULL) return NULL;

	do {
		d = readdir(dirs->rrddir);
		if (d == NULL) {
			stack_closedir();
		}
		else if (*(d->d_name) == '.') {
			d = NULL;
		}
		else {
			sprintf(fname, "%s/%s", dirs->dirname, d->d_name);
			if ((stat(fname, &st) == 0) && (S_ISDIR(st.st_mode))) {
				stack_opendir(fname);
				d = NULL;
			}
		}
	} while (dirs && (d == NULL));

	if (d == NULL) return NULL;

	if (strncmp(fname, "./", 2) == 0) return (fname + 2); else return fname;
}


static char *rrdlink_text(void *host, graph_t *rrd, hg_link_t wantmeta, time_t starttime, time_t endtime)
{
	static char *rrdlink = NULL;
	static int rrdlinksize = 0;
	char *graphdef, *p;
	char *hostdisplayname, *hostrrdgraphs;

	hostdisplayname = bbh_item(host, BBH_DISPLAYNAME);
	hostrrdgraphs = bbh_item(host, BBH_TRENDS);

	dbgprintf("rrdlink_text: host %s, rrd %s\n", bbh_item(host, BBH_HOSTNAME), rrd->gdef->hobbitrrdname);

	/* If no rrdgraphs definition, include all with default links */
	if (hostrrdgraphs == NULL) {
		dbgprintf("rrdlink_text: Standard URL (no rrdgraphs)\n");
		return hobbit_graph_data(bbh_item(host, BBH_HOSTNAME), hostdisplayname, NULL, -1, rrd->gdef, rrd->count, 
					 HG_WITH_STALE_RRDS, wantmeta, 0, starttime, endtime);
	}

	/* Find this rrd definition in the rrdgraphs */
	graphdef = strstr(hostrrdgraphs, rrd->gdef->hobbitrrdname);

	/* If not found ... */
	if (graphdef == NULL) {
		dbgprintf("rrdlink_text: NULL graphdef\n");

		/* Do we include all by default ? */
		if (*(hostrrdgraphs) == '*') {
			dbgprintf("rrdlink_text: Default URL included\n");

			/* Yes, return default link for this RRD */
			return hobbit_graph_data(bbh_item(host, BBH_HOSTNAME), hostdisplayname, NULL, -1, rrd->gdef, rrd->count, 
						 HG_WITH_STALE_RRDS, wantmeta, 0, starttime, endtime);
		}
		else {
			dbgprintf("rrdlink_text: Default URL NOT included\n");
			/* No, return empty string */
			return "";
		}
	}

	/* We now know that rrdgraphs explicitly define what to do with this RRD */

	/* Does he want to explicitly exclude this RRD ? */
	if ((graphdef > hostrrdgraphs) && (*(graphdef-1) == '!')) {
		dbgprintf("rrdlink_text: This graph is explicitly excluded\n");
		return "";
	}

	/* It must be included. */
	if (rrdlink == NULL) {
		rrdlinksize = 4096;
		rrdlink = (char *)malloc(rrdlinksize);
	}

	*rrdlink = '\0';

	p = graphdef + strlen(rrd->gdef->hobbitrrdname);
	if (*p == ':') {
		/* There is an explicit list of graphs to add for this RRD. */
		char savechar;
		char *enddef;
		graph_t *myrrd;
		char *partlink;

		myrrd = (graph_t *) malloc(sizeof(graph_t));
		myrrd->gdef = (hobbitgraph_t *) calloc(1, sizeof(hobbitgraph_t));

		/* First, null-terminate this graph definition so we only look at the active RRD */
		enddef = strchr(graphdef, ',');
		if (enddef) *enddef = '\0';

		graphdef = (p+1);
		do {
			p = strchr(graphdef, '|');			/* Ends at '|' ? */
			if (p == NULL) p = graphdef + strlen(graphdef);	/* Ends at end of string */
			savechar = *p; *p = '\0'; 

			myrrd->gdef->hobbitrrdname = graphdef;
			myrrd->gdef->hobbitpartname = NULL;
			myrrd->gdef->maxgraphs = 0;
			myrrd->count = rrd->count;
			myrrd->next = NULL;
			partlink = hobbit_graph_data(bbh_item(host, BBH_HOSTNAME), hostdisplayname, NULL, -1, myrrd->gdef, myrrd->count, 
						     HG_WITH_STALE_RRDS, wantmeta, 0, starttime, endtime);
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
		return hobbit_graph_data(bbh_item(host, BBH_HOSTNAME), hostdisplayname, NULL, -1, rrd->gdef, rrd->count, 
					 HG_WITH_STALE_RRDS, wantmeta, 0, starttime, endtime);
	}

	return "";
}


char *generate_trends(char *hostname, time_t starttime, time_t endtime)
{
	void *myhost;
	char hostrrddir[PATH_MAX];
	char *fn;
	int anyrrds = 0;
	hobbitgraph_t *graph;
	graph_t *rwalk;
	char *allrrdlinks = NULL, *allrrdlinksend;
	unsigned int allrrdlinksize = 0;

	myhost = hostinfo(hostname);
	if (!myhost) return NULL;

	sprintf(hostrrddir, "%s/%s", xgetenv("BBRRDS"), hostname);
	chdir(hostrrddir);
	stack_opendir(".");

	while ((fn = stack_readdir())) {
		/* Check if the filename ends in ".rrd", and we know how to handle this RRD */
		if ((strlen(fn) <= 4) || (strcmp(fn+strlen(fn)-4, ".rrd") != 0)) continue;
		graph = find_hobbit_graph(fn); if (!graph) continue;

		dbgprintf("Got RRD %s\n", fn);
		anyrrds++;

		for (rwalk = (graph_t *)bbh_item(myhost, BBH_DATA); (rwalk && (rwalk->gdef != graph)); rwalk = rwalk->next) ;
		if (rwalk == NULL) {
			graph_t *newrrd = (graph_t *) malloc(sizeof(graph_t));

			newrrd->gdef = graph;
			newrrd->count = 1;
			newrrd->next = (graph_t *)bbh_item(myhost, BBH_DATA);
			bbh_set_item(myhost, BBH_DATA, newrrd);
			rwalk = newrrd;
			dbgprintf("New rrd for host:%s, rrd:%s\n", hostname, graph->hobbitrrdname);
		}
		else {
			rwalk->count++;

			dbgprintf("Extra RRD for host %s, rrd %s   count:%d\n", 
				hostname, 
				rwalk->gdef->hobbitrrdname, rwalk->count);
		}
	}
	stack_closedir();

	if (!anyrrds) return NULL;

	allrrdlinksize = 16384;
	allrrdlinks = (char *) malloc(allrrdlinksize);
	*allrrdlinks = '\0';
	allrrdlinksend = allrrdlinks;

	graph = hobbitgraphs;
	while (graph->hobbitrrdname) {
		for (rwalk = (graph_t *)bbh_item(myhost, BBH_DATA); (rwalk && (rwalk->gdef->hobbitrrdname != graph->hobbitrrdname)); rwalk = rwalk->next) ;
		if (rwalk) {
			int buflen;
			char *onelink;

			buflen = (allrrdlinksend - allrrdlinks);
			onelink = rrdlink_text(myhost, rwalk, 0, starttime, endtime);
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

