/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module for Hobbit, responsible for loading the           */
/* hobbit-nkview.cfg file.                                                    */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: loadnkconf.c,v 1.1 2005-11-18 09:55:55 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#include "libbbgen.h"

static RbtHandle rbconf;

static int key_compare(void *a, void *b)
{
	return strcasecmp((char *)a, (char *)b);
}


int load_nkconfig(char *fn, char *wantclass)
{
	FILE *fd;
	char *inbuf = NULL;
	int inbufsz = 0;

	rbconf = rbtNew(key_compare);

	fd = stackfopen(fn, "r");
	if (fd == NULL) return 1;

	while (stackfgets(&inbuf, &inbufsz, "include", NULL)) {
		/* Class  Host  service  TIME  TTPrio TTGroup TTExtra */
		char *eclass, *ehost, *eservice, *etime, *ttgroup, *ttextra;
		int ttprio = 0;
		nkconf_t *newitem;
		RbtStatus status;

		eclass = gettok(inbuf, "|\n"); if (!eclass) continue;
		if (wantclass && eclass && (strcmp(eclass, wantclass) != 0)) continue;
		ehost = gettok(NULL, "|\n"); if (!ehost) continue;
		eservice = gettok(NULL, "|\n"); if (!eservice) continue;
		etime = gettok(NULL, "|\n"); if (!etime) continue;
		ttprio = atoi(gettok(NULL, "|\n"));
		ttgroup = gettok(NULL, "|\n");
		ttextra = gettok(NULL, "|\n");

		if ((ehost == NULL) || (eservice == NULL) || (ttprio == 0)) continue;
		if (etime && *etime && !within_sla(etime, 0)) continue;

		newitem = (nkconf_t *)malloc(sizeof(nkconf_t));
		newitem->key = (char *)malloc(strlen(ehost) + strlen(eservice) + 2);
		sprintf(newitem->key, "%s|%s", ehost, eservice);
		newitem->priority = ttprio;
		newitem->ttgroup = strdup(urlencode(ttgroup));
		newitem->ttextra = strdup(urlencode(ttextra));

		status = rbtInsert(rbconf, newitem->key, newitem);
	}

	stackfclose(fd);
	return 0;
}

nkconf_t *get_nkconfig(char *key)
{
	RbtHandle handle;
	void *k1, *k2;

	handle = rbtFind(rbconf, key);
	if (handle == rbtEnd(rbconf)) return NULL;

	rbtKeyValue(rbconf, handle, &k1, &k2);
	return (nkconf_t *)k2;
}

