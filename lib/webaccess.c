/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for web access control.                               */
/*                                                                            */
/* Copyright (C) 2011 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: misc.c 6712 2011-07-31 21:01:52Z storner $";

#include <string.h>

#include "config.h"
#include "libxymon.h"


void *acctree = NULL;

void *load_web_access_config(char *accessfn)
{
	FILE *fd;
	strbuffer_t *buf;

	if (acctree) return 0;
	acctree = xtreeNew(strcasecmp);

	fd = stackfopen(accessfn, "r", NULL);
	if (fd == NULL) return NULL;

	buf = newstrbuffer(0);
	while (stackfgets(buf, NULL)) {
		char *group, *member;
		char *key;

		group = strtok(STRBUF(buf), ": \n");
		if (!group) continue;

		member = strtok(NULL, ", \n");
		while (member) {
			key = (char *)malloc(strlen(group) + strlen(member) + 2);
			sprintf(key, "%s %s", group, member);
			xtreeAdd(acctree, key, NULL);
			member = strtok(NULL, ", \n");
		}
	}
	stackfclose(fd);

	return acctree;
}

int web_access_allowed(char *username, char *hostname, char *testname, web_access_type_t acc)
{

	void *hinfo;
	char *pages, *onepg, *key;

	hinfo = hostinfo(hostname);
	if (!hinfo || !acctree || !username) return 0;

	/* Check for "root" access first */
	key = (char *)malloc(strlen(username) + 6);
	sprintf(key, "root %s");
	if (xtreeFind(acctree, key) != xtreeEnd(acctree)) {
		xfree(key);
		return 1;
	}
	xfree(key);

	pages = strdup(xmh_item(hinfo, XMH_ALLPAGEPATHS));
	onepg = strtok(pages, ",");
	while (onepg) {
		char *p;

		p = strchr(onepg, '/'); if (p) *p = '\0'; /* Will only look at the top-level path element */

		key = (char *)malloc(strlen(onepg) + strlen(username) + 2);
		sprintf(key, "%s %s", onepg, username);
		if (xtreeFind(acctree, key) != xtreeEnd(acctree)) {
			xfree(key);
			xfree(pages);
			return 1;
		}

		xfree(key);
		onepg = strtok(NULL, ",");
	}

	xfree(pages);
	return 0;
}

