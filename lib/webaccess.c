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
		SBUF_DEFINE(key);

		group = strtok(STRBUF(buf), ": \n");
		if (!group) continue;

		member = strtok(NULL, ", \n");
		while (member) {
			SBUF_MALLOC(key, strlen(group) + strlen(member) + 2);
			snprintf(key, key_buflen, "%s %s", group, member);
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
	char *pages, *onepg;
	SBUF_DEFINE(key);

	hinfo = hostinfo(hostname);
	if (!hinfo || !acctree || !username) return 0;

	/* Check for "root" access first */
	SBUF_MALLOC(key, strlen(username) + 6);
	snprintf(key, key_buflen, "root %s", username);
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

		SBUF_MALLOC(key, strlen(onepg) + strlen(username) + 2);
		snprintf(key, key_buflen, "%s %s", onepg, username);
		if (xtreeFind(acctree, key) != xtreeEnd(acctree)) {
			xfree(key);
			xfree(pages);
			return 1;
		}

		xfree(key);
		onepg = strtok(NULL, ",");
	}
	xfree(pages);

	if (hostname) {
		/* See if user is a member of a group named by the hostname */
		SBUF_MALLOC(key, strlen(hostname) + strlen(username) + 2);
		snprintf(key, key_buflen, "%s %s", hostname, username);
		if (xtreeFind(acctree, key) != xtreeEnd(acctree)) {
			xfree(key);
			return 1;
		}
		xfree(key);
	}

	if (testname) {
		/* See if user is a member of a group named by the testname */
		SBUF_MALLOC(key, strlen(testname) + strlen(username) + 2);
		snprintf(key, key_buflen, "%s %s", testname, username);
		if (xtreeFind(acctree, key) != xtreeEnd(acctree)) {
			xfree(key);
			return 1;
		}
		xfree(key);
	}

	return 0;
}

