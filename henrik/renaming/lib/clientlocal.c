/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* client-local.cfg file into memory and finding the proper host entry.       */
/*                                                                            */
/* Copyright (C) 2006-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static strbuffer_t *clientconfigs = NULL;
static RbtHandle rbconfigs;

void load_clientconfig(void)
{
	static char *configfn = NULL;
	static void *clientconflist = NULL;
	FILE *fd;
	strbuffer_t *buf;
	char *sectstart;

	if (!configfn) {
		configfn = (char *)malloc(strlen(xgetenv("BBHOME"))+ strlen("/etc/client-local.cfg") + 1);
		sprintf(configfn, "%s/etc/client-local.cfg", xgetenv("BBHOME"));
	}

	/* First check if there were no modifications at all */
	if (clientconflist) {
		if (!stackfmodified(clientconflist)){
			dbgprintf("No files modified, skipping reload of %s\n", configfn);
			return;
		}
		else {
			stackfclist(&clientconflist);
			clientconflist = NULL;
		}
	}

	if (!clientconfigs) {
		clientconfigs = newstrbuffer(0);
	}
	else {
		rbtDelete(rbconfigs);
		clearstrbuffer(clientconfigs);
	}

	rbconfigs = rbtNew(name_compare);
	addtobuffer(clientconfigs, "\n");
	buf = newstrbuffer(0);

	fd = stackfopen(configfn, "r", &clientconflist); if (!fd) return;
	while (stackfgets(buf, NULL)) addtostrbuffer(clientconfigs, buf);
	stackfclose(fd);

	sectstart = strstr(STRBUF(clientconfigs), "\n[");
	while (sectstart) {
		char *key, *nextsect;

		sectstart += 2;
		key = sectstart;

		sectstart += strcspn(sectstart, "]\n");
		if (*sectstart == ']') {
			*sectstart = '\0'; sectstart++;
			sectstart += strcspn(sectstart, "\n");
		}

		nextsect = strstr(sectstart, "\n[");
		if (nextsect) *(nextsect+1) = '\0';

		rbtInsert(rbconfigs, key, sectstart+1);
		sectstart = nextsect;
	}

	freestrbuffer(buf);
}

char *get_clientconfig(char *hostname, char *hostclass, char *hostos)
{
	RbtIterator handle;
	char *result = NULL;

	if (!clientconfigs) return NULL;

	/*
	 * Find the client config.  Search for a HOSTNAME entry first, 
	 * then the CLIENTCLASS, then CLIENTOS.
	 */
	handle = rbtFind(rbconfigs, hostname);
	if ((handle == rbtEnd(rbconfigs)) && hostclass && *hostclass)
		handle = rbtFind(rbconfigs, hostclass);
	if ((handle == rbtEnd(rbconfigs)) && hostos && *hostos)
		handle = rbtFind(rbconfigs, hostos);

	if (handle != rbtEnd(rbconfigs)) result = (char *)gettreeitem(rbconfigs, handle);

	return result;
}

