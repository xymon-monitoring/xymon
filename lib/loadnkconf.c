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

static char rcsid[] = "$Id: loadnkconf.c,v 1.3 2006-01-20 13:53:54 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#include "libbbgen.h"

#define DEFAULTCONFIG "etc/hobbit-nkview.cfg"

static RbtHandle rbconf;
static char *defaultfn = NULL;

static int key_compare(void *a, void *b)
{
	return strcasecmp((char *)a, (char *)b);
}


int load_nkconfig(char *fn)
{
	static int firsttime = 1;
	FILE *fd;
	char *inbuf = NULL;
	int inbufsz = 0;

	if (!firsttime) {
		/* Clean up existing datatree */
		RbtHandle handle;

		for (handle = rbtBegin(rbconf); (handle != rbtEnd(rbconf)); handle = rbtNext(rbconf, handle)) {
			void *k1, *k2;
			char *key;

			rbtKeyValue(rbconf, handle, &k1, &k2);
			key = (char *)k1;
			if (*(key + strlen(key) - 1) == '=') {
				/* Clone record just holds a char string pointing to the origin record */
				char *pointsto = (char *)k2;
				xfree(pointsto);
			}
			else {
				/* Full record */
				nkconf_t *rec = (nkconf_t *)k2;
				if (rec->nktime)  xfree(rec->nktime);
				if (rec->ttgroup) xfree(rec->ttgroup);
				if (rec->ttextra) xfree(rec->ttextra);
			}
			xfree(key);
		}

		rbtDelete(rbconf);
	}

	firsttime = 0;
	rbconf = rbtNew(key_compare);

	if (!fn) {
		if (!defaultfn) {
			char *bbhome = xgetenv("BBHOME");
			defaultfn = (char *)malloc(strlen(bbhome) + strlen(DEFAULTCONFIG) + 2);
			sprintf(defaultfn, "%s/%s", bbhome, DEFAULTCONFIG);
		}
		fn = defaultfn;
	}

	fd = stackfopen(fn, "r");
	if (fd == NULL) return 1;

	while (stackfgets(&inbuf, &inbufsz, "include", NULL)) {
		/* Full record : Host  service  START  END  TIMESPEC  TTPrio TTGroup TTExtra */
		/* Clone record: Host  =HOST */
		char *ehost, *eservice, *estart, *eend, *etime, *ttgroup, *ttextra;
		int ttprio = 0;
		nkconf_t *newitem;
		RbtStatus status;
		int idx = 0;

		ehost = gettok(inbuf, "|\n"); if (!ehost) continue;
		eservice = gettok(NULL, "|\n"); if (!eservice) continue;

		if (*eservice == '=') {
			char *key = (char *)malloc(strlen(ehost) + 2);
			char *pointsto = strdup(eservice+1);

			sprintf(key, "%s=", ehost);
			status = rbtInsert(rbconf, key, pointsto);
		}
		else {
			estart = gettok(NULL, "|\n"); if (!estart) continue;
			eend = gettok(NULL, "|\n"); if (!eend) continue;
			etime = gettok(NULL, "|\n"); if (!etime) continue;
			ttprio = atoi(gettok(NULL, "|\n")); if (ttprio == 0) continue;
			ttgroup = gettok(NULL, "|\n");
			ttextra = gettok(NULL, "|\n");

			newitem = (nkconf_t *)malloc(sizeof(nkconf_t));
			newitem->key = (char *)malloc(strlen(ehost) + strlen(eservice) + 15);
			sprintf(newitem->key, "%s|%s", ehost, eservice);
			newitem->starttime= ((estart && *estart) ? atoi(estart) : 0);
			newitem->endtime  = ((eend && *eend) ? atoi(eend) : 0);
			newitem->nktime   = ((etime && *etime) ? strdup(etime) : NULL);
			newitem->priority = ttprio;
			newitem->ttgroup  = strdup(ttgroup);
			newitem->ttextra  = strdup(ttextra);

			status = rbtInsert(rbconf, newitem->key, newitem);
			while (status == RBT_STATUS_DUPLICATE_KEY) {
				idx++;
				sprintf(newitem->key, "%s|%s|%d", ehost, eservice, idx);
				status = rbtInsert(rbconf, newitem->key, newitem);
			}
		}
	}

	stackfclose(fd);

	if (debug) {
		RbtHandle handle;

		handle = rbtBegin(rbconf);
		while (handle != rbtEnd(rbconf)) {
			void *k1, *k2;
			rbtKeyValue(rbconf, handle, &k1, &k2);
			printf("%s\n", (char *)k1);
			handle = rbtNext(rbconf, handle);
		}
	}

	return 0;
}

RbtHandle findrec(char *key)
{
	RbtHandle handle;

	handle = rbtFind(rbconf, key);
	if (handle == rbtEnd(rbconf)) {
		/* Check if there's a clone pointer record */
		char *clonekey, *p;

		clonekey = strdup(key);
		p = strchr(clonekey, '|'); 
		if (p && *(p+1)) { *p = '='; *(p+1) = '\0'; }
		handle = rbtFind(rbconf, clonekey);
		xfree(clonekey);

		if (handle != rbtEnd(rbconf)) {
			void *k1, *k2;
			char *pointsto;
			char *service;

			/* Get the origin record for this cloned record, using the same service name */
			rbtKeyValue(rbconf, handle, &k1, &k2);
			pointsto = (char *)k2;
			service = strchr(key, '|'); if (service) service++;
			clonekey = (char *)malloc(strlen(pointsto) + strlen(service) + 2);
			sprintf(clonekey, "%s|%s", pointsto, service);

			handle = rbtFind(rbconf, clonekey);
			xfree(clonekey);
		}
	}

	return handle;
}

int timecheck(time_t starttime, time_t endtime, char *nktime)
{
	time_t now = getcurrenttime(NULL);

	if (starttime && (now < starttime)) return 0;
	if (endtime && (now > endtime)) return 0;
	if ((nktime == NULL) || within_sla(nktime, 0)) return 1;

	return 0;
}

nkconf_t *get_nkconfig(char *key, int flags)
{
	static RbtHandle handle;
	void *k1, *k2;
	char *realkey;
	nkconf_t *result = NULL;

	switch (flags) {
	  case NKCONF_TIMEFILTER:
		handle = findrec(key);
		/* We may have hit a cloned record, so use the real key for further searches */
		if (handle != rbtEnd(rbconf)) {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			realkey = k1;
		}

		while (handle != rbtEnd(rbconf)) {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			result = (nkconf_t *)k2;
			if (timecheck(result->starttime, result->endtime, result->nktime)) return result;

			/* Go to the next */
			handle = rbtNext(rbconf, handle);
			if (handle != rbtEnd(rbconf)) {
				rbtKeyValue(rbconf, handle, &k1, &k2);
				if (strncmp(key, ((nkconf_t *)k2)->key, strlen(realkey)) != 0) handle=rbtEnd(rbconf);
			}
		}
		break;

	  case NKCONF_FIRSTMATCH:
		handle = findrec(key);
		break;

	  case NKCONF_NEXTMATCH:
		rbtKeyValue(rbconf, handle, &k1, &k2);
		realkey = (char *)k1;
		handle = rbtNext(rbconf, handle);
		if (handle != rbtEnd(rbconf)) {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			if (strncmp(key, ((nkconf_t *)k2)->key, strlen(realkey)) != 0) handle=rbtEnd(rbconf);
		}
		break;

	  case NKCONF_RAW_FIRST:
		handle = rbtBegin(rbconf);
		break;

	  case NKCONF_RAW_NEXT:
		handle = rbtNext(rbconf, handle);
		break;
	}

	if (handle == rbtEnd(rbconf)) return NULL;

	rbtKeyValue(rbconf, handle, &k1, &k2);
	result = (nkconf_t *)k2;

	return result;
}

