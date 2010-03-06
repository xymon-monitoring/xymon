/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* Various utility functions specific to bbgen. Generally useful code is      */
/* in the library.                                                            */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: util.c 6125 2009-02-12 13:09:34Z storner $";

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

static RbtHandle hosttree;
static int havehosttree = 0;
static RbtHandle columntree;
static int havecolumntree = 0;

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


host_t *find_host(char *hostname)
{
	RbtIterator handle;

	if (havehosttree == 0) return NULL;

	/* Search for the host */
	handle = rbtFind(hosttree, hostname);
	if (handle != rbtEnd(hosttree)) {
		hostlist_t *entry = (hostlist_t *)gettreeitem(hosttree, handle);
		return (entry ? entry->hostentry : NULL);
	}
	
	return NULL;
}

int host_exists(char *hostname)
{
	return (find_host(hostname) != NULL);
}

hostlist_t *find_hostlist(char *hostname)
{
	RbtIterator handle;

	if (havehosttree == 0) return NULL;

	/* Search for the host */
	handle = rbtFind(hosttree, hostname);
	if (handle != rbtEnd(hosttree)) {
		hostlist_t *entry = (hostlist_t *)gettreeitem(hosttree, handle);
		return entry;
	}
	
	return NULL;
}

void add_to_hostlist(hostlist_t *rec)
{
	if (havehosttree == 0) {
		hosttree = rbtNew(name_compare);
		havehosttree = 1;
	}

	rbtInsert(hosttree, rec->hostentry->hostname, rec);
}

static RbtIterator hostlistwalk;
hostlist_t *hostlistBegin(void)
{
	if (havehosttree == 0) return NULL;

	hostlistwalk = rbtBegin(hosttree);

	if (hostlistwalk != rbtEnd(hosttree)) {
		return (hostlist_t *)gettreeitem(hosttree, hostlistwalk);
	}
	else {
		return NULL;
	}
}

hostlist_t *hostlistNext(void)
{
	if (havehosttree == 0) return NULL;

	if (hostlistwalk != rbtEnd(hosttree)) hostlistwalk = rbtNext(hosttree, hostlistwalk);

	if (hostlistwalk != rbtEnd(hosttree)) {
		return (hostlist_t *)gettreeitem(hosttree, hostlistwalk);
	}
	else {
		return NULL;
	}
}

bbgen_col_t *find_or_create_column(char *testname, int create)
{
	bbgen_col_t *newcol = NULL;
	RbtIterator handle;

	dbgprintf("find_or_create_column(%s)\n", textornull(testname));

	if (havecolumntree == 0) {
		columntree = rbtNew(name_compare);
		havecolumntree = 1;
	}

	handle = rbtFind(columntree, testname);
	if (handle != rbtEnd(columntree)) newcol = (bbgen_col_t *)gettreeitem(columntree, handle);

	if (newcol == NULL) {
		if (!create) return NULL;

		newcol = (bbgen_col_t *) calloc(1, sizeof(bbgen_col_t));
		newcol->name = strdup(testname);
		newcol->listname = (char *)malloc(strlen(testname)+1+2); 
		sprintf(newcol->listname, ",%s,", testname);

		rbtInsert(columntree, newcol->name, newcol);
	}

	return newcol;
}


int wantedcolumn(char *current, char *wanted)
{
	char *tag;
	int result;

	tag = (char *) malloc(strlen(current)+3);
	sprintf(tag, "|%s|", current);
	result = (strstr(wanted, tag) != NULL);

	xfree(tag);
	return result;
}

