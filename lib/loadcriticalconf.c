/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* critical.cfg file.                                                         */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <utime.h>

#include "libxymon.h"

#define DEFAULTCONFIG "etc/critical.cfg"

static RbtHandle rbconf;
static char *defaultfn = NULL;
static char *configfn = NULL;


static void flushrec(void *k1, void *k2)
{
	char *key;

	key = (char *)k1;
	if (*(key + strlen(key) - 1) == '=') {
		/* Clone record just holds a char string pointing to the origin record */
		char *pointsto = (char *)k2;
		xfree(pointsto);
	}
	else {
		/* Full record */
		critconf_t *rec = (critconf_t *)k2;
		if (rec->crittime)  xfree(rec->crittime);
		if (rec->ttgroup) xfree(rec->ttgroup);
		if (rec->ttextra) xfree(rec->ttextra);
	}
	xfree(key);
}

int load_critconfig(char *fn)
{
	static void *configfiles = NULL;
	static int firsttime = 1;
	FILE *fd;
	strbuffer_t *inbuf;

	/* Setup the default configuration filename */
	if (!fn) {
		if (!defaultfn) {
			char *xymonhome = xgetenv("XYMONHOME");
			defaultfn = (char *)malloc(strlen(xymonhome) + strlen(DEFAULTCONFIG) + 2);
			sprintf(defaultfn, "%s/%s", xymonhome, DEFAULTCONFIG);
		}
		fn = defaultfn;
	}

	if (configfn) xfree(configfn);
	configfn = strdup(fn);

	/* First check if there were no modifications at all */
	if (configfiles) {
		if (!stackfmodified(configfiles)){
			dbgprintf("No files modified, skipping reload of %s\n", fn);
			return 0;
		}
		else {
			stackfclist(&configfiles);
			configfiles = NULL;
		}
	}

	if (!firsttime) {
		/* Clean up existing datatree */
		RbtHandle handle;
		void *k1, *k2;

		for (handle = rbtBegin(rbconf); (handle != rbtEnd(rbconf)); handle = rbtNext(rbconf, handle)) {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			flushrec(k1, k2);
		}

		rbtDelete(rbconf);
	}

	firsttime = 0;
	rbconf = rbtNew(name_compare);

	fd = stackfopen(fn, "r", &configfiles);
	if (fd == NULL) return 1;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		/* Full record : Host  service  START  END  TIMESPEC  TTPrio TTGroup TTExtra */
		/* Clone record: Host  =HOST */
		char *ehost, *eservice, *estart, *eend, *etime, *ttgroup, *ttextra, *updinfo;
		int ttprio = 0;
		critconf_t *newitem;
		RbtStatus status;
		int idx = 0;

		ehost = gettok(STRBUF(inbuf), "|\n"); if (!ehost) continue;
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
			updinfo = gettok(NULL, "|\n");

			newitem = (critconf_t *)malloc(sizeof(critconf_t));
			newitem->key = (char *)malloc(strlen(ehost) + strlen(eservice) + 15);
			sprintf(newitem->key, "%s|%s", ehost, eservice);
			newitem->starttime= ((estart && *estart) ? atoi(estart) : 0);
			newitem->endtime  = ((eend && *eend) ? atoi(eend) : 0);
			newitem->crittime = ((etime && *etime) ? strdup(etime) : NULL);
			newitem->priority = ttprio;
			newitem->ttgroup  = strdup(ttgroup);
			newitem->ttextra  = strdup(ttextra);
			newitem->updinfo  = strdup(updinfo);

			status = rbtInsert(rbconf, newitem->key, newitem);
			while (status == RBT_STATUS_DUPLICATE_KEY) {
				idx++;
				sprintf(newitem->key, "%s|%s|%d", ehost, eservice, idx);
				status = rbtInsert(rbconf, newitem->key, newitem);
			}
		}
	}

	stackfclose(fd);
	freestrbuffer(inbuf);

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

static RbtHandle findrec(char *key)
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

static int timecheck(time_t starttime, time_t endtime, char *crittime)
{
	time_t now = getcurrenttime(NULL);

	if (starttime && (now < starttime)) return 0;
	if (endtime && (now > endtime)) return 0;
	if ((crittime == NULL) || within_sla(NULL, crittime, 0)) return 1; /* FIXME */

	return 0;
}

critconf_t *get_critconfig(char *key, int flags, char **resultkey)
{
	static RbtHandle handle;
	static char *realkey = NULL;
	void *k1, *k2;
	critconf_t *result = NULL;
	int isclone;

	if (resultkey) *resultkey = NULL;

	switch (flags) {
	  case CRITCONF_TIMEFILTER:
		handle = findrec(key);
		/* We may have hit a cloned record, so use the real key for further searches */
		if (handle != rbtEnd(rbconf)) {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			realkey = k1;
		}

		while (handle != rbtEnd(rbconf)) {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			result = (critconf_t *)k2;
			if (timecheck(result->starttime, result->endtime, result->crittime)) return result;

			/* Go to the next */
			handle = rbtNext(rbconf, handle);
			if (handle != rbtEnd(rbconf)) {
				rbtKeyValue(rbconf, handle, &k1, &k2);
				if (strncmp(realkey, ((critconf_t *)k2)->key, strlen(realkey)) != 0) handle=rbtEnd(rbconf);
			}
		}
		realkey = NULL;
		break;

	  case CRITCONF_FIRSTMATCH:
		handle = findrec(key);
		realkey = NULL;
		if (handle != rbtEnd(rbconf)) {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			realkey = (char *)k1;
		}
		break;

	  case CRITCONF_FIRST:
		realkey = NULL;
		handle = rbtBegin(rbconf);
		if (handle == rbtEnd(rbconf)) return NULL;
		do {
			rbtKeyValue(rbconf, handle, &k1, &k2);
			realkey = (char *)k1;
			isclone = (*(realkey + strlen(realkey) - 1) == '=');
			if (isclone) handle = rbtNext(rbconf, handle);
		} while (isclone && (handle != rbtEnd(rbconf)));
		break;


	  case CRITCONF_NEXT:
		if (!realkey || (handle == rbtEnd(rbconf))) return NULL;
		isclone = 1;
		while (isclone && (handle != rbtEnd(rbconf))) {
			handle = rbtNext(rbconf, handle);
			if (handle) {
				rbtKeyValue(rbconf, handle, &k1, &k2);
				realkey = (char *)k1;
				isclone = (*(realkey + strlen(realkey) - 1) == '=');
			}
		}
		break;

	  case CRITCONF_RAW_FIRST:
		handle = rbtBegin(rbconf);
		realkey = NULL;
		break;

	  case CRITCONF_RAW_NEXT:
		handle = rbtNext(rbconf, handle);
		realkey = NULL;
		break;

	  case CRITCONF_FIRSTHOSTMATCH:
		do {
			int found = 0;
			char *delim;

			realkey = NULL;
			handle = rbtBegin(rbconf);
			while (!found && (handle != rbtEnd(rbconf))) {
				rbtKeyValue(rbconf, handle, &k1, &k2);
				realkey = (char *)k1;
				delim = realkey + strlen(key); /* OK even if past end of realkey */
				found = ((strncmp(realkey, key, strlen(key)) == 0) &&
					((*delim == '|') || (*delim == '=')));
				if (!found) { handle = rbtNext(rbconf, handle); realkey = NULL; }
			}

			if ((handle != rbtEnd(rbconf)) && (*(realkey + strlen(realkey) - 1) == '=')) {
				key = (char *)k2;
				isclone = 1;
			}
			else isclone = 0;

		} while (isclone && (handle != rbtEnd(rbconf)));
		break;
	}

	if (handle == rbtEnd(rbconf)) { realkey = NULL; return NULL; }

	rbtKeyValue(rbconf, handle, &k1, &k2);
	if (resultkey) *resultkey = (char *)k1;
	result = (critconf_t *)k2;

	return result;
}

int update_critconfig(critconf_t *rec)
{
	char *bakfn;
	FILE *bakfd;
	unsigned char buf[8192];
	int n;
	struct stat st;
	struct utimbuf ut;

	RbtHandle handle;
	FILE *fd;
	int result = 0;

	/* First, copy the old file */
	bakfn = (char *)malloc(strlen(configfn) + 5);
	sprintf(bakfn, "%s.bak", configfn);
	if (stat(configfn, &st) == 0) {
		ut.actime = st.st_atime;
		ut.modtime = st.st_mtime;
	}
	else ut.actime = ut.modtime = getcurrenttime(NULL);
	fd = fopen(configfn, "r");
	if (fd) {
		bakfd = fopen(bakfn, "w");
		if (bakfd) {
			while ((n = fread(buf, 1, sizeof(buf), fd)) > 0) fwrite(buf, 1, n, bakfd);
			fclose(bakfd);
			utime(bakfn, &ut);
		}
		fclose(fd);
	}
	xfree(bakfn);

	fd = fopen(configfn, "w");
	if (fd == NULL) {
		errprintf("Cannot open output file %s\n", configfn);
		return 1;
	}

	if (rec) {
		handle = rbtFind(rbconf, rec->key);
		if (handle == rbtEnd(rbconf)) rbtInsert(rbconf, rec->key, rec);
	}

	handle = rbtBegin(rbconf);
	while (handle != rbtEnd(rbconf)) {
		void *k1, *k2;
		char *onekey;

		rbtKeyValue(rbconf, handle, &k1, &k2);
		onekey = (char *)k1;

		if (*(onekey + strlen(onekey) - 1) == '=') {
			char *pointsto = (char *)k2;
			char *hostname;
			
			hostname = strdup(onekey);
			*(hostname + strlen(hostname) - 1) = '\0';
			fprintf(fd, "%s|=%s\n", hostname, pointsto);
		}
		else {
			critconf_t *onerec = (critconf_t *)k2;
			char startstr[20], endstr[20];

			*startstr = *endstr = '\0';
			if (onerec->starttime > 0) sprintf(startstr, "%d", (int)onerec->starttime);
			if (onerec->endtime > 0) sprintf(endstr, "%d", (int)onerec->endtime);

			fprintf(fd, "%s|%s|%s|%s|%d|%s|%s|%s\n",
				onekey, 
				startstr, endstr,
				(onerec->crittime ? onerec->crittime : ""),
				onerec->priority, 
				(onerec->ttgroup ? onerec->ttgroup : ""), 
				(onerec->ttextra ? onerec->ttextra : ""),
				(onerec->updinfo ? onerec->updinfo : ""));
		}

		handle = rbtNext(rbconf, handle);
	}

	fclose(fd);

	return result;
}

void addclone_critconfig(char *origin, char *newclone)
{
	char *newkey;
	RbtHandle handle;

	newkey = (char *)malloc(strlen(newclone) + 2);
	sprintf(newkey, "%s=", newclone);
	handle = rbtFind(rbconf, newkey);
	if (handle != rbtEnd(rbconf)) dropclone_critconfig(newclone);
	rbtInsert(rbconf, newkey, strdup(origin));
}

void dropclone_critconfig(char *drop)
{
	RbtHandle handle;
	char *key;
	void *k1, *k2;
	char *dropkey, *dropsrc;

	key = (char *)malloc(strlen(drop) + 2);
	sprintf(key, "%s=", drop);
	handle = rbtFind(rbconf, key);
	if (handle == rbtEnd(rbconf)) return;

	rbtKeyValue(rbconf, handle, &k1, &k2);
	dropkey = k1; dropsrc = k2;
	rbtErase(rbconf, handle);
	xfree(dropkey); xfree(dropsrc);

	xfree(key);
}

int delete_critconfig(char *dropkey, int evenifcloned)
{
	RbtHandle handle;
	void *k1, *k2;

	handle = rbtFind(rbconf, dropkey);
	if (handle == rbtEnd(rbconf)) return 0;

	if (!evenifcloned) {
		/* Check if this record has any clones attached to it */
		char *hostname, *p;

		hostname = strdup(dropkey);
		p = strchr(hostname, '|'); if (p) *p = '\0';

		handle = rbtBegin(rbconf);

		while (handle != rbtEnd(rbconf)) {
			void *k1, *k2;
			char *key, *ptr;

			rbtKeyValue(rbconf, handle, &k1, &k2);
			key = (char *)k1; ptr = (char *)k2;
			if ((*(key + strlen(key) - 1) == '=') && (strcmp(hostname, ptr) == 0)) {
				xfree(hostname);
				return 1;
			}

			handle = rbtNext(rbconf, handle);
		}

		xfree(hostname);
	}

	handle = rbtFind(rbconf, dropkey);
	if (handle != rbtEnd(rbconf)) {
		rbtKeyValue(rbconf, handle, &k1, &k2);
		rbtErase(rbconf, handle);
		flushrec(k1, k2);
	}

	return 0;
}

