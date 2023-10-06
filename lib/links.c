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
static void * linkstree;
STATIC_SBUF_DEFINE(notesskin);
STATIC_SBUF_DEFINE(helpskin);
static char *columndocurl = NULL;
static char *hostdocurl = NULL;

char *link_docext(char *fn)
{
	char *p = strrchr(fn, '.');
	if (p == NULL) return NULL;

	if ( (strcasecmp(p, ".php") == 0)    ||
             (strcasecmp(p, ".php3") == 0)   ||
             (strcasecmp(p, ".asp") == 0)    ||
             (strcasecmp(p, ".pdf") == 0)    ||
             (strcasecmp(p, ".doc") == 0)    ||
             (strcasecmp(p, ".docx") == 0)    ||
             (strcasecmp(p, ".odt") == 0)    ||
	     (strcasecmp(p, ".shtml") == 0)  ||
	     (strcasecmp(p, ".phtml") == 0)  ||
	     (strcasecmp(p, ".html") == 0)   ||
	     (strcasecmp(p, ".htm") == 0))      {
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

static void load_links(char *directory, char *urlprefix, int softfail)
{
	DIR		*linkdir;
	struct dirent 	*d;
	char		fn[PATH_MAX];

	dbgprintf("load_links(%s, %s)\n", textornull(directory), textornull(urlprefix));

	linkdir = opendir(directory);
	if (!linkdir) {
		if (softfail) { dbgprintf("Cannot read links in directory %s\n", directory); }
		else errprintf("Cannot read links in directory %s\n", directory);
		return;
	}

	while ((d = readdir(linkdir))) {
		link_t *newlink;

		if (*(d->d_name) == '.') continue;

		strncpy(fn, d->d_name, sizeof(fn));
		newlink = init_link(fn, urlprefix);
		xtreeAdd(linkstree, newlink->key, newlink);
	}
	closedir(linkdir);
}

void load_all_links(void)
{
	char dirname[PATH_MAX];
	char *p;

	dbgprintf("load_all_links()\n");

	linkstree = xtreeNew(strcasecmp);

	if (notesskin) { xfree(notesskin); notesskin = NULL; }
	if (helpskin) { xfree(helpskin); helpskin = NULL; }
	if (columndocurl) { xfree(columndocurl); columndocurl = NULL; }
	if (hostdocurl) { xfree(hostdocurl); hostdocurl = NULL; }

	if (xgetenv("XYMONNOTESSKIN")) notesskin = strdup(xgetenv("XYMONNOTESSKIN"));
	else { 
		SBUF_MALLOC(notesskin, strlen(xgetenv("XYMONWEB")) + strlen("/notes") + 1);
		snprintf(notesskin, notesskin_buflen, "%s/notes", xgetenv("XYMONWEB"));
	}

	if (xgetenv("XYMONHELPSKIN")) helpskin = strdup(xgetenv("XYMONHELPSKIN"));
	else { 
		SBUF_MALLOC(helpskin, strlen(xgetenv("XYMONWEB")) + strlen("/help") + 1);
		snprintf(helpskin, helpskin_buflen, "%s/help", xgetenv("XYMONWEB"));
	}

	if (xgetenv("COLUMNDOCURL")) columndocurl = strdup(xgetenv("COLUMNDOCURL"));
	if (xgetenv("HOSTDOCURL")) hostdocurl = strdup(xgetenv("HOSTDOCURL"));

	if (!hostdocurl || (strlen(hostdocurl) == 0)) {
		strcpy(dirname, xgetenv("XYMONNOTESDIR"));
		load_links(dirname, notesskin, 1);
	}

	/* If no XYMONHELPDIR, change xxx/xxx/xxx/notes into xxx/xxx/xxx/help */
	if (xgetenv("XYMONHELPDIR")) strcpy(dirname, xgetenv("XYMONHELPDIR"));
	else {
		strncpy(dirname, xgetenv("XYMONNOTESDIR"), sizeof(dirname));
		dirname[sizeof(dirname)-1] = '\0'; /* Make sure it is null terminated */
		p = strrchr(dirname, '/'); *p = '\0'; strncat(dirname, "/help", (sizeof(dirname) - strlen(dirname)));
	}
	load_links(dirname, helpskin, 0);

	linksloaded = 1;
}


static link_t *find_link(char *key)
{
	link_t *l = NULL;
	xtreePos_t handle;

	handle = xtreeFind(linkstree, key);
	if (handle != xtreeEnd(linkstree)) l = (link_t *)xtreeData(linkstree, handle);

	return l;
}


char *columnlink(char *colname)
{
	STATIC_SBUF_DEFINE(linkurl);
	link_t *link;

	if (linkurl == NULL) SBUF_MALLOC(linkurl, PATH_MAX);
	if (!linksloaded) load_all_links();

	link = find_link(colname);
	if (link) {
		snprintf(linkurl, linkurl_buflen, "%s/%s", link->urlprefix, link->filename);
	}
	else if (columndocurl) {
		snprintf(linkurl, linkurl_buflen, columndocurl, colname);
	}
	else {
		*linkurl = '\0';
	}

	return linkurl;
}


char *hostlink(char *hostname)
{
	STATIC_SBUF_DEFINE(linkurl);
	link_t *link;

	if (linkurl == NULL) SBUF_MALLOC(linkurl, PATH_MAX);
	if (!linksloaded) load_all_links();

	if (hostdocurl && *hostdocurl) {
		snprintf(linkurl, linkurl_buflen, hostdocurl, hostname);
		return linkurl;
	}
	else {
		link = find_link(hostname);

		if (link) {
			snprintf(linkurl, linkurl_buflen, "%s/%s", link->urlprefix, link->filename);
			return linkurl;
		}
	}

	return NULL;
}

