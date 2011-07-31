/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the host-,     */
/* page-, and column-links present in www/notes and www/help.                 */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <dirent.h>

#include "libxymon.h"

/* Info-link definitions. */
typedef struct link_t {
	char	*key;
	char	*filename;
	char	*urlprefix;	/* "/help", "/notes" etc. */
} link_t;

static int linksloaded = 0;
static RbtHandle linkstree;
static char *notesskin = NULL;
static char *helpskin = NULL;
static char *columndocurl = NULL;

char *link_docext(char *fn)
{
	char *p = strrchr(fn, '.');
	if (p == NULL) return NULL;

	if ( (strcmp(p, ".php") == 0)    ||
             (strcmp(p, ".php3") == 0)   ||
             (strcmp(p, ".asp") == 0)    ||
             (strcmp(p, ".doc") == 0)    ||
	     (strcmp(p, ".shtml") == 0)  ||
	     (strcmp(p, ".phtml") == 0)  ||
	     (strcmp(p, ".html") == 0)   ||
	     (strcmp(p, ".htm") == 0))      {
		return p;
	}

	return NULL;
}

static link_t *init_link(char *filename, char *urlprefix)
{
	char *p;
	link_t *newlink = NULL;

	dbgprintf("init_link(%s, %s)\n", textornull(filename), textornull(urlprefix));

	newlink = (link_t *) malloc(sizeof(link_t));
	newlink->filename = strdup(filename);
	newlink->urlprefix = urlprefix;

	/* Without extension, this time */
	p = link_docext(filename);
	if (p) *p = '\0';
	newlink->key = strdup(filename);

	return newlink;
}

static void load_links(char *directory, char *urlprefix)
{
	DIR		*linkdir;
	struct dirent 	*d;
	char		fn[PATH_MAX];

	dbgprintf("load_links(%s, %s)\n", textornull(directory), textornull(urlprefix));

	linkdir = opendir(directory);
	if (!linkdir) {
		errprintf("Cannot read links in directory %s\n", directory);
		return;
	}

	MEMDEFINE(fn);

	while ((d = readdir(linkdir))) {
		link_t *newlink;

		if (*(d->d_name) == '.') continue;

		strcpy(fn, d->d_name);
		newlink = init_link(fn, urlprefix);
		rbtInsert(linkstree, newlink->key, newlink);
	}
	closedir(linkdir);

	MEMUNDEFINE(fn);
}

void load_all_links(void)
{
	char dirname[PATH_MAX];
	char *p;

	MEMDEFINE(dirname);

	dbgprintf("load_all_links()\n");

	linkstree = rbtNew(name_compare);

	if (notesskin) { xfree(notesskin); notesskin = NULL; }
	if (helpskin) { xfree(helpskin); helpskin = NULL; }
	if (columndocurl) { xfree(columndocurl); columndocurl = NULL; }

	if (xgetenv("XYMONNOTESSKIN")) notesskin = strdup(xgetenv("XYMONNOTESSKIN"));
	else { 
		notesskin = (char *) malloc(strlen(xgetenv("XYMONWEB")) + strlen("/notes") + 1);
		sprintf(notesskin, "%s/notes", xgetenv("XYMONWEB"));
	}

	if (xgetenv("XYMONHELPSKIN")) helpskin = strdup(xgetenv("XYMONHELPSKIN"));
	else { 
		helpskin = (char *) malloc(strlen(xgetenv("XYMONWEB")) + strlen("/help") + 1);
		sprintf(helpskin, "%s/help", xgetenv("XYMONWEB"));
	}

	if (xgetenv("COLUMNDOCURL")) columndocurl = strdup(xgetenv("COLUMNDOCURL"));

	strcpy(dirname, xgetenv("XYMONNOTESDIR"));
	load_links(dirname, notesskin);

	/* Change xxx/xxx/xxx/notes into xxx/xxx/xxx/help */
	p = strrchr(dirname, '/'); *p = '\0'; strcat(dirname, "/help");
	load_links(dirname, helpskin);

	linksloaded = 1;

	MEMUNDEFINE(dirname);
}


static link_t *find_link(char *key)
{
	link_t *l = NULL;
	RbtIterator handle;

	handle = rbtFind(linkstree, key);
	if (handle != rbtEnd(linkstree)) l = (link_t *)gettreeitem(linkstree, handle);

	return l;
}


char *columnlink(char *colname)
{
	static char *linkurl = NULL;
	link_t *link;

	if (linkurl == NULL) linkurl = (char *)malloc(PATH_MAX);
	if (!linksloaded) load_all_links();

	link = find_link(colname);
	if (link) {
		sprintf(linkurl, "%s/%s", link->urlprefix, link->filename);
	}
	else if (columndocurl) {
		sprintf(linkurl, columndocurl, colname);
	}
	else {
		*linkurl = '\0';
	}

	return linkurl;
}


char *hostlink(char *hostname)
{
	static char *linkurl = NULL;
	link_t *link;

	if (linkurl == NULL) linkurl = (char *)malloc(PATH_MAX);
	if (!linksloaded) load_all_links();

	link = find_link(hostname);

	if (link) {
		sprintf(linkurl, "%s/%s", link->urlprefix, link->filename);
		return linkurl;
	}

	return NULL;
}

char *hostlink_filename(char *hostname)
{
	link_t *link;

	if (!linksloaded) load_all_links();

	link = find_link(hostname);

	return (link ? link->filename : NULL);
}

