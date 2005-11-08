/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* Various utility functions specific to bbgen. Generally useful code is      */
/* in the library.                                                            */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: util.c,v 1.149 2005-11-08 13:37:25 henrik Exp $";

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <utime.h>
#include <unistd.h>

#include "bbgen.h"
#include "util.h"

char *htmlextension = ".html"; /* Filename extension for generated HTML files */
hostlist_t      *hosthead = NULL;


char *hostpage_link(host_t *host)
{
	/* Provide a link to the page where this host lives, relative to BBWEB */

	static char pagelink[PATH_MAX];
	char tmppath[PATH_MAX];
	bbgen_page_t *pgwalk;

	if (host->parent && (strlen(((bbgen_page_t *)host->parent)->name) > 0)) {
		sprintf(pagelink, "%s%s", ((bbgen_page_t *)host->parent)->name, htmlextension);
		for (pgwalk = host->parent; (pgwalk); pgwalk = pgwalk->parent) {
			if (strlen(pgwalk->name)) {
				sprintf(tmppath, "%s/%s", pgwalk->name, pagelink);
				strcpy(pagelink, tmppath);
			}
		}
	}
	else {
		sprintf(pagelink, "bb%s", htmlextension);
	}

	return pagelink;
}


char *hostpage_name(host_t *host)
{
	/* Provide a link to the page where this host lives */

	static char pagename[PATH_MAX];
	char tmpname[PATH_MAX];
	bbgen_page_t *pgwalk;

	if (host->parent && (strlen(((bbgen_page_t *)host->parent)->name) > 0)) {
		pagename[0] = '\0';
		for (pgwalk = host->parent; (pgwalk); pgwalk = pgwalk->parent) {
			if (strlen(pgwalk->name)) {
				strcpy(tmpname, pgwalk->title);
				if (strlen(pagename)) {
					strcat(tmpname, "/");
					strcat(tmpname, pagename);
				}
				strcpy(pagename, tmpname);
			}
		}
	}
	else {
		sprintf(pagename, "Top page");
	}

	return pagename;
}



static int checknopropagation(char *testname, char *noproptests)
{
	if (noproptests == NULL) return 0;

	if (strcmp(noproptests, ",*,") == 0) return 1;
	if (strstr(noproptests, testname) != NULL) return 1;

	return 0;
}

int checkpropagation(host_t *host, char *test, int color, int acked)
{
	/* NB: Default is to propagate test, i.e. return 1 */
	char *testname;
	int result = 1;

	if (!host) return 1;

	testname = (char *) malloc(strlen(test)+3);
	sprintf(testname, ",%s,", test);
	if (acked) {
		if (checknopropagation(testname, host->nopropacktests)) result = 0;
	}

	if (result) {
		if (color == COL_RED) {
			if (checknopropagation(testname, host->nopropredtests)) result = 0;
		}
		else if (color == COL_YELLOW) {
			if (checknopropagation(testname, host->nopropyellowtests)) result = 0;
			if (checknopropagation(testname, host->nopropredtests)) result = 0;
		}
		else if (color == COL_PURPLE) {
			if (checknopropagation(testname, host->noproppurpletests)) result = 0;
		}
	}

	xfree(testname);
	return result;
}



char *urldoclink(const char *docurl, const char *hostname)
{
	/*
	 * docurl is a user defined text string to build
	 * a documentation url. It is expanded with the
	 * hostname.
	 */

	static char linkurl[PATH_MAX];

	if (docurl) {
		sprintf(linkurl, docurl, hostname);
	}
	else {
		linkurl[0] = '\0';
	}

	return linkurl;
}



host_t *find_host(const char *hostname)
{
	static hostlist_t *lastsearch = NULL;
	hostlist_t	*l;

	/* We cache the last result */
	if (lastsearch && (strcmp(lastsearch->hostentry->hostname, hostname) == 0)) 
		return lastsearch->hostentry;

	/* Search for the host */
	for (l=hosthead; (l && (strcmp(l->hostentry->hostname, hostname) != 0)); l = l->next) ;
	lastsearch = l;

	return (l ? l->hostentry : NULL);
}


bbgen_col_t *find_or_create_column(const char *testname, int create)
{
	static bbgen_col_t *colhead = NULL;	/* Head of column-name list */
	static bbgen_col_t *lastcol = NULL;	/* Cache the last lookup */
	bbgen_col_t *newcol;

	dprintf("find_or_create_column(%s)\n", textornull(testname));
	if (lastcol && (strcmp(testname, lastcol->name) == 0))
		return lastcol;

	for (newcol = colhead; (newcol && (strcmp(testname, newcol->name) != 0)); newcol = newcol->next);
	if (newcol == NULL) {
		if (!create) return NULL;

		newcol = (bbgen_col_t *) malloc(sizeof(bbgen_col_t));
		newcol->name = strdup(testname);
		newcol->listname = (char *)malloc(strlen(testname)+1+2); sprintf(newcol->listname, ",%s,", testname);

		/* No need to maintain this list in order */
		if (colhead == NULL) {
			colhead = newcol;
			newcol->next = NULL;
		}
		else {
			newcol->next = colhead;
			colhead = newcol;
		}
	}
	lastcol = newcol;

	return newcol;
}


char *histlogurl(char *hostname, char *service, time_t histtime)
{
	static char url[PATH_MAX];

	/* cgi-bin/bb-histlog.sh?HOST=SLS-P-CE1.slsdomain.sls.dk&SERVICE=msgs&TIMEBUF=Fri_Nov_7_16:01:08_2002 */
	sprintf(url, "%s/bb-histlog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s", 
		xgetenv("CGIBINURL"), hostname, service, histlogtime(histtime));

	return url;
}
