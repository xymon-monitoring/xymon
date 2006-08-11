/*----------------------------------------------------------------------------*/
/* Hobbit CGI for administering the hobbit-nkview.cfg file                    */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-nkedit.c,v 1.14 2006-08-11 13:07:52 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libbbgen.h"

static char *operator = NULL;

static enum { NKEDIT_FIND, NKEDIT_NEXT, NKEDIT_UPDATE, NKEDIT_DELETE, NKEDIT_ADDCLONE, NKEDIT_DROPCLONE } editaction = NKEDIT_FIND;
static char *rq_hostname = NULL;
static char *rq_service = NULL;
static int rq_priority = 0;
static char *rq_group = NULL;
static char *rq_extra = NULL;
static char *rq_nktime = NULL;
static time_t rq_start = 0;
static time_t rq_end = 0;
static char *rq_clonestoadd = NULL;
static char *rq_clonestodrop = NULL;
static int  rq_dropevenifcloned = 0;

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;
	char *rq_nkwkdays = NULL;
	char *rq_nkslastart = NULL;
	char *rq_nkslaend = NULL;
	int  rq_startday = 0;
	int  rq_startmon = 0;
	int  rq_startyear = 0;
	int  rq_endday = 0;
	int  rq_endmon = 0;
	int  rq_endyear = 0;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "Find") == 0) {
			editaction = NKEDIT_FIND;
		}
		else if (strcasecmp(cwalk->name, "Next") == 0) {
			editaction = NKEDIT_NEXT;
		}
		else if (strcasecmp(cwalk->name, "Update") == 0) {
			editaction = NKEDIT_UPDATE;
		}
		else if (strcasecmp(cwalk->name, "Drop") == 0) {
			editaction = NKEDIT_DELETE;
		}
		else if (strcasecmp(cwalk->name, "Clone") == 0) {
			/* The "clone" button does both things */
			editaction = NKEDIT_ADDCLONE;
		}
		else if (strcasecmp(cwalk->name, "HOSTNAME") == 0) {
			if (*cwalk->value) rq_hostname = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "SERVICE") == 0) {
			if (*cwalk->value) rq_service = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "PRIORITY") == 0) {
			rq_priority = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "GROUP") == 0) {
			if (*cwalk->value) rq_group = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NKWKDAYS") == 0) {
			if (*cwalk->value) {
				if (!rq_nkwkdays) rq_nkwkdays = strdup(cwalk->value);
				else {
					rq_nkwkdays = (char *)realloc(rq_nkwkdays, strlen(rq_nkwkdays) + strlen(cwalk->value) + 1);
					strcat(rq_nkwkdays, cwalk->value);
				}
			}
		}
		else if (strcasecmp(cwalk->name, "NKSTARTHOUR") == 0) {
			if (*cwalk->value) rq_nkslastart = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NKENDHOUR") == 0) {
			if (*cwalk->value) rq_nkslaend = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "start-day") == 0) {
			rq_startday = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "start-mon") == 0) {
			rq_startmon = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "start-yr") == 0) {
			rq_startyear = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "end-day") == 0) {
			rq_endday = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "end-mon") == 0) {
			rq_endmon = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "end-yr") == 0) {
			rq_endyear = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXTRA") == 0) {
			if (*cwalk->value) rq_extra = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "DROPEVENIFCLONED") == 0) {
			rq_dropevenifcloned = 1;
		}
		else if (strcasecmp(cwalk->name, "NKEDITADDCLONES") == 0) {
			if (*cwalk->value) rq_clonestoadd = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NKEDITCLONELIST") == 0) {
			if (rq_clonestodrop) {
				rq_clonestodrop = (char *)realloc(rq_clonestodrop, strlen(rq_clonestodrop) + strlen(cwalk->value) + 2);
				strcat(rq_clonestodrop, " ");
				strcat(rq_clonestodrop, cwalk->value);
			}
			else {
				if (*cwalk->value) rq_clonestodrop = strdup(cwalk->value);
			}
		}

		cwalk = cwalk->next;
	}

	if (editaction == NKEDIT_UPDATE) {
		struct tm tm;

		if ((rq_startday == 0) || (rq_startmon == 0) || (rq_startyear == 0))
			rq_start = -1;
		else {
			memset(&tm, 0, sizeof(tm));
			tm.tm_mday = rq_startday;
			tm.tm_mon = rq_startmon - 1;
			tm.tm_year = rq_startyear - 1900;
			tm.tm_isdst = -1;
			rq_start = mktime(&tm);
		}

		if ((rq_endday == 0) || (rq_endmon == 0) || (rq_endyear == 0))
			rq_end = -1;
		else {
			memset(&tm, 0, sizeof(tm));
			tm.tm_mday = rq_endday;
			tm.tm_mon = rq_endmon - 1;
			tm.tm_year = rq_endyear - 1900;
			tm.tm_isdst = -1;
			rq_end = mktime(&tm);
		}

		rq_nktime = (char *)malloc(strlen(rq_nkwkdays) + strlen(rq_nkslastart) + strlen(rq_nkslaend) + 3);
		sprintf(rq_nktime, "%s:%s:%s", rq_nkwkdays, rq_nkslastart, rq_nkslaend);
	}
	else if (editaction == NKEDIT_ADDCLONE) {
		if (!rq_clonestoadd && rq_clonestodrop) editaction = NKEDIT_DROPCLONE;
	}
}

void findrecord(char *hostname, char *service, char *nodatawarning, char *isclonewarning, char *hascloneswarning)
{
	nkconf_t *rec = NULL;
	int isaclone = 0;
	int hasclones = 0;
	char warnmsg[4096];

	/* Setup the list of cloned records */
	sethostenv_nkclonelist_clear();

	if (hostname && *hostname) {
		char *key, *realkey, *clonekey;
		nkconf_t *clonerec;

		if (service && *service) {
			/* First check if the host+service is really a clone of something else */
			key = (char *)malloc(strlen(hostname) + strlen(service) + 2);
			sprintf(key, "%s|%s", hostname, service);
			rec = get_nkconfig(key, NKCONF_FIRSTMATCH, &realkey);
		}
		else {
			key = strdup(hostname);
			rec = get_nkconfig(key, NKCONF_FIRSTHOSTMATCH, &realkey);
		}

		if (rec && realkey && (strcmp(key, realkey) != 0)) {
			char *p;

			xfree(key);
			key = strdup(realkey);
			hostname = realkey;
			p = strchr(realkey, '|');
			if (p) {
				*p = '\0';
				service = p+1;
			}

			isaclone = 1;
		}
		xfree(key);

		/* Next, see what hosts are clones of this one */
		clonerec = get_nkconfig(NULL, NKCONF_RAW_FIRST, &clonekey);
		while (clonerec) {
			if ((*(clonekey + strlen(clonekey) -1) == '=') && (strcmp(hostname, (char *)clonerec) == 0)) {
				sethostenv_nkclonelist_add(clonekey);
				hasclones = 1;
			}
			clonerec = get_nkconfig(NULL, NKCONF_RAW_NEXT, &clonekey);
		}
	}
	else {
		hostname = "";
	}

	if (!service || !(*service)) service="";

	if (rec) sethostenv_nkedit(rec->updinfo, rec->priority, rec->ttgroup, rec->starttime, rec->endtime, rec->nktime, rec->ttextra);
	else sethostenv_nkedit("", 0, NULL, 0, 0, NULL, NULL);

	sethostenv(hostname, "", service, colorname(COL_BLUE), NULL);

	*warnmsg = '\0';
	if (!rec && nodatawarning) sprintf(warnmsg, "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('%s'); </SCRIPT>\n", nodatawarning);
	if (isaclone && isclonewarning) sprintf(warnmsg, "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('%s'); </SCRIPT>\n", isclonewarning);
	if (hasclones && hascloneswarning) sprintf(warnmsg, "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('%s'); </SCRIPT>\n", hascloneswarning);

	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	showform(stdout, "nkedit", "nkedit_form", COL_BLUE, getcurrenttime(NULL), warnmsg, NULL);
}


void nextrecord(char *hostname, char *service, char *isclonewarning, char *hascloneswarning)
{
	nkconf_t *rec;
	char *nexthost, *nextservice;

	/* First check if the host+service is really a clone of something else */
	if (hostname && service) {
		char *key;

		key = (char *)malloc(strlen(hostname) + strlen(service) + 2);
		sprintf(key, "%s|%s", hostname, service);
		rec = get_nkconfig(key, NKCONF_FIRSTMATCH, NULL);
		if (rec) rec = get_nkconfig(NULL, NKCONF_NEXT, NULL);
		xfree(key);
	}
	else {
		rec = get_nkconfig(NULL, NKCONF_FIRST, NULL);
	}

	if (rec) {
		nexthost = strdup(rec->key);
		nextservice = strchr(nexthost, '|'); if (nextservice) { *nextservice = '\0'; nextservice++; }
	}
	else {
		nexthost = strdup("");
		nextservice = "";
	}

	findrecord(nexthost, nextservice, NULL, isclonewarning, hascloneswarning);
	xfree(nexthost);
}

void updaterecord(char *hostname, char *service)
{
	nkconf_t *rec = NULL;

	if (hostname && service) {
		char *key = (char *)malloc(strlen(hostname) + strlen(service) + 2);
		char *realkey;
		char datestr[20];
		time_t now = getcurrenttime(NULL);

		strftime(datestr, sizeof(datestr), "%Y-%m-%d %H:%M:%S", localtime(&now));
		sprintf(key, "%s|%s", hostname, service);
		rec = get_nkconfig(key, NKCONF_FIRSTMATCH, &realkey);
		if (rec == NULL) {
			rec = (nkconf_t *)calloc(1, sizeof(nkconf_t));
			rec->key = strdup(key);
		}
		rec->priority = rq_priority;
		rec->starttime = (rq_start > 0) ? rq_start : 0;
		rec->endtime = (rq_end > 0) ? rq_end : 0;

		if (rec->nktime) {
			xfree(rec->nktime); rec->nktime = NULL;
		}

		if (rq_nktime) {
			rec->nktime = (strcmp(rq_nktime, "*:0000:2400") == 0) ? NULL : strdup(rq_nktime);
		}

		if (rec->ttgroup) xfree(rec->ttgroup); 
		rec->ttgroup = (rq_group ? strdup(rq_group) : NULL);
		if (rec->ttextra) xfree(rec->ttextra); 
		rec->ttextra = (rq_extra ? strdup(rq_extra) : NULL);
		if (rec->updinfo) xfree(rec->updinfo);
		rec->updinfo = (char *)malloc(strlen(operator) + strlen(datestr) + 2);
		sprintf(rec->updinfo, "%s %s", operator, datestr);

		update_nkconfig(rec);
		xfree(key);
	}

	findrecord(hostname, service, NULL, NULL, NULL);
}

void addclone(char *origin, char *newhosts, char *service)
{
	char *newclone;

	newclone = strtok(newhosts, " ");
	while (newclone) {
		addclone_nkconfig(origin, newclone);
		newclone = strtok(NULL, " ");
	}

	update_nkconfig(NULL);
	findrecord(origin, service, NULL, NULL, NULL);
}

void dropclone(char *origin, char *drops, char *service)
{
	char *drop;

	drop = strtok(drops, " ");
	while (drop) {
		dropclone_nkconfig(drop);
		drop = strtok(NULL, " ");
	}

	update_nkconfig(NULL);
	findrecord(origin, service, NULL, NULL, NULL);
}

void deleterecord(char *hostname, char *service, int evenifcloned)
{
	char *key;

	key = (char *)malloc(strlen(hostname) + strlen(service) + 2);
	sprintf(key, "%s|%s", hostname, service);
	if (delete_nkconfig(key, evenifcloned) == 0) {
		update_nkconfig(NULL);
	}

	findrecord(hostname, service, NULL, NULL, 
		   (evenifcloned ? "Warning: Orphans will be ignored" : "Will not delete record that is cloned"));
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *configfn = NULL;

	operator = getenv("REMOTE_USER");
	if (!operator) operator = "Anonymous";

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');
			configfn = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	redirect_cgilog("hobbit-nkedit");
	parse_query();
	load_nkconfig(configfn);

	switch (editaction) {
	  case NKEDIT_FIND:
		findrecord(rq_hostname, rq_service, 
			   ((rq_hostname && rq_service) ? "No record for this host/service" : NULL),
			   "Cloned - showing master record", NULL);
		break;

	  case NKEDIT_NEXT:
		nextrecord(rq_hostname, rq_service, "Cloned - showing master record", NULL);
		break;

	  case NKEDIT_UPDATE:
		updaterecord(rq_hostname, rq_service);
		break;

	  case NKEDIT_DELETE:
		deleterecord(rq_hostname, rq_service, rq_dropevenifcloned);
		break;

	  case NKEDIT_ADDCLONE:
		addclone(rq_hostname, rq_clonestoadd, rq_service);
		break;

	  case NKEDIT_DROPCLONE:
		dropclone(rq_hostname, rq_clonestodrop, rq_service);
		break;
	}

	return 0;
}

