/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* critical.cfg file.                                                         */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
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

static void * rbconf;
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
			defaultfn = (char *)malloc(strlen(xymonhome) + strlen(DEFAULT_CRITCONFIGFN) + 2);
			sprintf(defaultfn, "%s/%s", xymonhome, DEFAULT_CRITCONFIGFN);
		}
		fn = defaultfn;
	}

	if (configfn && (strcmp(fn, configfn) != 0)) {
		/* Force full reload - it's a different config file */
		if (configfiles) {
			stackfclist(&configfiles);
			configfiles = NULL;
		}
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
		xtreePos_t handle;

		for (handle = xtreeFirst(rbconf); (handle != xtreeEnd(rbconf)); handle = xtreeNext(rbconf, handle)) {
			flushrec(xtreeKey(rbconf, handle), xtreeData(rbconf, handle));
		}

		xtreeDestroy(rbconf);
	}

	firsttime = 0;
	rbconf = xtreeNew(strcasecmp);

	fd = stackfopen(fn, "r", &configfiles);
	if (fd == NULL) return 1;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		/* Full record : Host  service  START  END  TIMESPEC  TTPrio TTGroup TTExtra */
		/* Clone record: Host  =HOST */
		char *ehost, *eservice, *estart, *eend, *etime, *ttgroup, *ttextra, *updinfo;
		int ttprio = 0;
		critconf_t *newitem;
		xtreeStatus_t status;
		int idx = 0;

		ehost = gettok(STRBUF(inbuf), "|\n"); if (!ehost) continue;
		eservice = gettok(NULL, "|\n"); if (!eservice) continue;

		if (*eservice == '=') {
			char *key = (char *)malloc(strlen(ehost) + 2);
			char *pointsto = strdup(eservice+1);

			sprintf(key, "%s=", ehost);
			status = xtreeAdd(rbconf, key, pointsto);
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

			status = xtreeAdd(rbconf, newitem->key, newitem);
			while (status == XTREE_STATUS_DUPLICATE_KEY) {
				idx++;
				sprintf(newitem->key, "%s|%s|%d", ehost, eservice, idx);
				status = xtreeAdd(rbconf, newitem->key, newitem);
			}
		}
	}

	stackfclose(fd);
	freestrbuffer(inbuf);

	if (debug) {
		xtreePos_t handle;

		handle = xtreeFirst(rbconf);
		while (handle != xtreeEnd(rbconf)) {
			printf("%s\n", (char *)xtreeKey(rbconf, handle));
			handle = xtreeNext(rbconf, handle);
		}
	}

	return 0;
}

static xtreePos_t findrec(char *key)
{
	xtreePos_t handle;

	handle = xtreeFind(rbconf, key);
	if (handle == xtreeEnd(rbconf)) {
		/* Check if there's a clone pointer record */
		char *clonekey, *p;

		clonekey = strdup(key);
		p = strchr(clonekey, '|'); 
		if (p && *(p+1)) { *p = '='; *(p+1) = '\0'; }
		handle = xtreeFind(rbconf, clonekey);
		xfree(clonekey);

		if (handle != xtreeEnd(rbconf)) {
			char *pointsto;
			char *service;

			/* Get the origin record for this cloned record, using the same service name */
			pointsto = (char *)xtreeData(rbconf, handle);
			service = strchr(key, '|'); 
			if (service) {
				service++;
				clonekey = (char *)malloc(strlen(pointsto) + strlen(service) + 2);
				sprintf(clonekey, "%s|%s", pointsto, service);

				handle = xtreeFind(rbconf, clonekey);
				xfree(clonekey);
			}
			else
				handle = xtreeEnd(rbconf);
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
	static xtreePos_t handle;
	static char *realkey = NULL;
	critconf_t *result = NULL;
	int isclone;

	if (resultkey) *resultkey = NULL;

	switch (flags) {
	  case CRITCONF_TIMEFILTER:
		handle = findrec(key);
		/* We may have hit a cloned record, so use the real key for further searches */
		if (handle != xtreeEnd(rbconf)) {
			realkey = (char *)xtreeKey(rbconf, handle);
		}

		while (handle != xtreeEnd(rbconf)) {
			result = (critconf_t *)xtreeData(rbconf, handle);
			if (timecheck(result->starttime, result->endtime, result->crittime)) return result;

			/* Go to the next */
			handle = xtreeNext(rbconf, handle);
			if (handle != xtreeEnd(rbconf)) {
				critconf_t *rec = (critconf_t *)xtreeData(rbconf, handle);
				if (strncmp(realkey, rec->key, strlen(realkey)) != 0) handle=xtreeEnd(rbconf);
			}
		}
		realkey = NULL;
		break;

	  case CRITCONF_FIRSTMATCH:
		handle = findrec(key);
		realkey = NULL;
		if (handle != xtreeEnd(rbconf)) {
			realkey = (char *)xtreeKey(rbconf, handle);
		}
		break;

	  case CRITCONF_FIRST:
		realkey = NULL;
		handle = xtreeFirst(rbconf);
		if (handle == xtreeEnd(rbconf)) return NULL;
		do {
			realkey = (char *)xtreeKey(rbconf, handle);
			isclone = (*(realkey + strlen(realkey) - 1) == '=');
			if (isclone) handle = xtreeNext(rbconf, handle);
		} while (isclone && (handle != xtreeEnd(rbconf)));
		break;


	  case CRITCONF_NEXT:
		if (!realkey || (handle == xtreeEnd(rbconf))) return NULL;
		isclone = 1;
		while (isclone && (handle != xtreeEnd(rbconf))) {
			handle = xtreeNext(rbconf, handle);
			if (handle) {
				realkey = (char *)xtreeKey(rbconf, handle);
				isclone = (*(realkey + strlen(realkey) - 1) == '=');
			}
		}
		break;

	  case CRITCONF_RAW_FIRST:
		handle = xtreeFirst(rbconf);
		realkey = NULL;
		break;

	  case CRITCONF_RAW_NEXT:
		handle = xtreeNext(rbconf, handle);
		realkey = NULL;
		break;

	  case CRITCONF_FIRSTHOSTMATCH:
		do {
			int found = 0;
			char *delim;

			realkey = NULL;
			handle = xtreeFirst(rbconf);
			while (!found && (handle != xtreeEnd(rbconf))) {
				realkey = (char *)xtreeKey(rbconf, handle);
				delim = realkey + strlen(key); /* OK even if past end of realkey */
				found = ((strncmp(realkey, key, strlen(key)) == 0) &&
					((*delim == '|') || (*delim == '=')));
				if (!found) { handle = xtreeNext(rbconf, handle); realkey = NULL; }
			}

			if ((handle != xtreeEnd(rbconf)) && (*(realkey + strlen(realkey) - 1) == '=')) {
				key = (char *)xtreeData(rbconf, handle);
				isclone = 1;
			}
			else isclone = 0;

		} while (isclone && (handle != xtreeEnd(rbconf)));
		break;
	}

	if (handle == xtreeEnd(rbconf)) { realkey = NULL; return NULL; }

	if (resultkey) *resultkey = (char *)xtreeKey(rbconf, handle);
	result = (critconf_t *)xtreeData(rbconf, handle);

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

	xtreePos_t handle;
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
		handle = xtreeFind(rbconf, rec->key);
		if (handle == xtreeEnd(rbconf)) xtreeAdd(rbconf, rec->key, rec);
	}

	handle = xtreeFirst(rbconf);
	while (handle != xtreeEnd(rbconf)) {
		char *onekey;

		onekey = (char *)xtreeKey(rbconf, handle);

		if (*(onekey + strlen(onekey) - 1) == '=') {
			char *pointsto = (char *)xtreeData(rbconf, handle);
			char *hostname;
			
			hostname = strdup(onekey);
			*(hostname + strlen(hostname) - 1) = '\0';
			fprintf(fd, "%s|=%s\n", hostname, pointsto);
		}
		else {
			critconf_t *onerec = (critconf_t *)xtreeData(rbconf, handle);
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

		handle = xtreeNext(rbconf, handle);
	}

	fclose(fd);

	return result;
}

void addclone_critconfig(char *origin, char *newclone)
{
	char *newkey;
	xtreePos_t handle;

	newkey = (char *)malloc(strlen(newclone) + 2);
	sprintf(newkey, "%s=", newclone);
	handle = xtreeFind(rbconf, newkey);
	if (handle != xtreeEnd(rbconf)) dropclone_critconfig(newclone);
	xtreeAdd(rbconf, newkey, strdup(origin));
}

void dropclone_critconfig(char *drop)
{
	xtreePos_t handle;
	char *key;
	char *dropkey, *dropsrc;

	key = (char *)malloc(strlen(drop) + 2);
	sprintf(key, "%s=", drop);
	handle = xtreeFind(rbconf, key);
	if (handle == xtreeEnd(rbconf)) return;

	dropkey = (char *)xtreeKey(rbconf, handle);
	dropsrc = (char *)xtreeDelete(rbconf, key);
	xfree(dropkey); xfree(dropsrc);

	xfree(key);
}

int delete_critconfig(char *dropkey, int evenifcloned)
{
	xtreePos_t handle;

	handle = xtreeFind(rbconf, dropkey);
	if (handle == xtreeEnd(rbconf)) return 0;

	if (!evenifcloned) {
		/* Check if this record has any clones attached to it */
		char *hostname, *p;

		hostname = strdup(dropkey);
		p = strchr(hostname, '|'); if (p) *p = '\0';

		handle = xtreeFirst(rbconf);

		while (handle != xtreeEnd(rbconf)) {
			char *key, *ptr;

			key = (char *)xtreeKey(rbconf, handle);
			ptr = (char *)xtreeData(rbconf, handle);
			if ((*(key + strlen(key) - 1) == '=') && (strcmp(hostname, ptr) == 0)) {
				xfree(hostname);
				return 1;
			}

			handle = xtreeNext(rbconf, handle);
		}

		xfree(hostname);
	}

	handle = xtreeFind(rbconf, dropkey);
	if (handle != xtreeEnd(rbconf)) {
		void *k1, *k2;

		k1 = xtreeKey(rbconf, handle);
		k2 = xtreeDelete(rbconf, dropkey);
		flushrec(k1, k2);
	}

	return 0;
}

