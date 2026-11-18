/*----------------------------------------------------------------------------*/
/* Xymon CGI for administering the critical.cfg file                          */
/*                                                                            */
/* Copyright (C) 2006-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libxymon.h"

static char *operator = NULL;

static enum { CRITEDIT_FIND, CRITEDIT_NEXT, CRITEDIT_UPDATE, CRITEDIT_DELETE, CRITEDIT_ADDCLONE, CRITEDIT_DROPCLONE } editaction = CRITEDIT_FIND;
static char *rq_hostname = NULL;
static char *rq_service = NULL;
static int rq_priority = 0;
static char *rq_group = NULL;
static char *rq_extra = NULL;
STATIC_SBUF_DEFINE(rq_crittime);
static time_t rq_start = 0;
static time_t rq_end = 0;
static char *rq_clonestoadd = NULL;
static char *rq_clonestodrop = NULL;
static int  rq_dropevenifcloned = 0;

/*
 * What the last change did, shown above the form: "Saved", "Deleted", or why
 * not. It is shown together with findrecord()'s own notes about clones, not
 * instead of them. Anything the user typed goes in escaped.
 */
static char statusmsg[2048] = "";

static void setstatus(const char *fmt, const char *arg)
{
	/* htmlattrquoted(), not htmlquoted(): a space stays a space, so it wraps */
	snprintf(statusmsg, sizeof(statusmsg), fmt, (arg ? htmlattrquoted((char *)arg) : ""));
}

/* After update_critconfig() returned rc, with errno saying why */
static void setsaveresult(int rc, const char *done)
{
	char why[1024];
	int e = errno;		/* before getpwuid(), which may change it */
	struct passwd *pw;

	switch (rc) {
	  case CRITSAVE_OK:
		setstatus("%s", done);
		return;

	  case CRITSAVE_NOBACKUP:
		snprintf(why, sizeof(why), "%s, but critical.cfg.bak could not be written: %s", done, strerror(e));
		setstatus("%s", why);
		return;

	  case CRITSAVE_NOOPEN:
		pw = getpwuid(geteuid());
		snprintf(why, sizeof(why),
			 "NOT SAVED: cannot write critical.cfg: %s. The web server runs as %s, which must be able to write critical.cfg and critical.cfg.bak",
			 strerror(e), (pw ? pw->pw_name : "an unknown user"));
		setstatus("%s", why);
		return;

	  case CRITSAVE_NOREAD:
		pw = getpwuid(geteuid());
		snprintf(why, sizeof(why),
			 "NOT SAVED: cannot read critical.cfg: %s. The web server runs as %s, which must be able to read and write critical.cfg, and write critical.cfg.bak",
			 strerror(e), (pw ? pw->pw_name : "an unknown user"));
		setstatus("%s", why);
		return;

	  case CRITSAVE_WRITEFAIL:
		snprintf(why, sizeof(why),
			 "NOT SAVED: writing critical.cfg failed: %s. It was put back as it was before this change",
			 strerror(e));
		setstatus("%s", why);
		return;

	  default:
		snprintf(why, sizeof(why),
			 "NOT SAVED: writing critical.cfg failed: %s, and it could not be put back. It may be incomplete: check it, and critical.cfg.bak",
			 strerror(e));
		setstatus("%s", why);
		return;
	}
}

/*
 * A record in critical.cfg is one line, its fields split on '|'. A value
 * from the form that held '|', CR or LF would add a field or a whole record;
 * replace those with a space, so a value stays inside its field and its line.
 */
static void critsanitize(char *s)
{
	for (; (s && *s); s++)
		if ((*s == '|') || (*s == '\n') || (*s == '\r')) *s = ' ';
}

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;
	char *rq_critwkdays = NULL;
	char *rq_critslastart = NULL;
	char *rq_critslaend = NULL;
	int  rq_startday = 0;
	int  rq_startmon = 0;
	int  rq_startyear = 0;
	int  rq_endday = 0;
	int  rq_endmon = 0;
	int  rq_endyear = 0;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "Find") == 0) {
			editaction = CRITEDIT_FIND;
		}
		else if (strcasecmp(cwalk->name, "Next") == 0) {
			editaction = CRITEDIT_NEXT;
		}
		else if (strcasecmp(cwalk->name, "Update") == 0) {
			editaction = CRITEDIT_UPDATE;
		}
		else if (strcasecmp(cwalk->name, "Drop") == 0) {
			editaction = CRITEDIT_DELETE;
		}
		else if (strcasecmp(cwalk->name, "Clone") == 0) {
			/* The "clone" button does both things */
			editaction = CRITEDIT_ADDCLONE;
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
		else if (strcasecmp(cwalk->name, "CRITWKDAYS") == 0) {
			if (*cwalk->value) {
				if (!rq_critwkdays) rq_critwkdays = strdup(cwalk->value);
				else {
					rq_critwkdays = (char *)realloc(rq_critwkdays, strlen(rq_critwkdays) + strlen(cwalk->value) + 1);
					strcat(rq_critwkdays, cwalk->value);
				}
			}
		}
		else if (strcasecmp(cwalk->name, "CRITSTARTHOUR") == 0) {
			if (*cwalk->value) rq_critslastart = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "CRITENDHOUR") == 0) {
			if (*cwalk->value) rq_critslaend = strdup(cwalk->value);
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
		else if (strcasecmp(cwalk->name, "CRITEDITADDCLONES") == 0) {
			if (*cwalk->value) rq_clonestoadd = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "CRITEDITCLONELIST") == 0) {
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

	critsanitize(rq_hostname);
	critsanitize(rq_service);
	critsanitize(rq_group);
	critsanitize(rq_extra);
	critsanitize(rq_clonestoadd);
	critsanitize(rq_clonestodrop);

	if (editaction == CRITEDIT_UPDATE) {
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

		/*
		 * The form always sends all three; a request missing one gets the
		 * form's own defaults, every day and all day, instead of a crash.
		 */
		if (!rq_critwkdays) rq_critwkdays = strdup("*");
		if (!rq_critslastart) rq_critslastart = strdup("0000");
		if (!rq_critslaend) rq_critslaend = strdup("2400");

		SBUF_MALLOC(rq_crittime,strlen(rq_critwkdays) + strlen(rq_critslastart) + strlen(rq_critslaend) + 3);
		snprintf(rq_crittime, rq_crittime_buflen, "%s:%s:%s", rq_critwkdays, rq_critslastart, rq_critslaend);
	}
	else if (editaction == CRITEDIT_ADDCLONE) {
		if (!rq_clonestoadd && rq_clonestodrop) editaction = CRITEDIT_DROPCLONE;
	}
}

void findrecord(char *hostname, char *service, char *nodatawarning, char *isclonewarning, char *hascloneswarning)
{
	critconf_t *rec = NULL;
	int isaclone = 0;
	int hasclones = 0;
	char warnmsg[4096];

	/* Setup the list of cloned records */
	sethostenv_critclonelist_clear();

	if (hostname && *hostname) {
		SBUF_DEFINE(key);
		char *realkey, *clonekey;
		critconf_t *clonerec;

		if (service && *service) {
			/* First check if the host+service is really a clone of something else */
			SBUF_MALLOC(key, strlen(hostname) + strlen(service) + 2);
			snprintf(key, key_buflen, "%s|%s", hostname, service);
			rec = get_critconfig(key, CRITCONF_FIRSTMATCH, &realkey);
		}
		else {
			key = strdup(hostname);
			rec = get_critconfig(key, CRITCONF_FIRSTHOSTMATCH, &realkey);
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
		clonerec = get_critconfig(NULL, CRITCONF_RAW_FIRST, &clonekey);
		while (clonerec) {
			if ((*(clonekey + strlen(clonekey) -1) == '=') && (strcmp(hostname, (char *)clonerec) == 0)) {
				sethostenv_critclonelist_add(clonekey);
				hasclones = 1;
			}
			clonerec = get_critconfig(NULL, CRITCONF_RAW_NEXT, &clonekey);
		}
	}
	else {
		hostname = "";
	}

	if (!service || !(*service)) service="";

	if (rec) sethostenv_critedit(rec->updinfo, rec->priority, rec->ttgroup, rec->starttime, rec->endtime, rec->crittime, rec->ttextra);
	else sethostenv_critedit("", 0, NULL, 0, 0, NULL, NULL);

	sethostenv(hostname, "", service, colorname(COL_BLUE), NULL);

	*warnmsg = '\0';
	if (!rec && nodatawarning) snprintf(warnmsg, sizeof(warnmsg), "%s", nodatawarning);
	if (isaclone && isclonewarning) snprintf(warnmsg, sizeof(warnmsg), "%s", isclonewarning);
	if (hasclones && hascloneswarning) snprintf(warnmsg, sizeof(warnmsg), "%s", hascloneswarning);

	/* The result of the change first, then the note about clones */
	if (*statusmsg) {
		char note[sizeof(warnmsg)];

		snprintf(note, sizeof(note), "%s", warnmsg);
		snprintf(warnmsg, sizeof(warnmsg), "%s%s%s", statusmsg, (*note ? "<BR>" : ""), note);
	}

	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	showform(stdout, "critedit", "critedit_form", COL_BLUE, getcurrenttime(NULL), warnmsg, NULL);
}


void nextrecord(char *hostname, char *service, char *isclonewarning, char *hascloneswarning)
{
	critconf_t *rec;
	char *nexthost, *nextservice;

	/* First check if the host+service is really a clone of something else */
	if (hostname && service) {
		SBUF_DEFINE(key);

		SBUF_MALLOC(key, strlen(hostname) + strlen(service) + 2);
		snprintf(key, key_buflen, "%s|%s", hostname, service);
		rec = get_critconfig(key, CRITCONF_FIRSTMATCH, NULL);
		if (rec) rec = get_critconfig(NULL, CRITCONF_NEXT, NULL);
		xfree(key);
	}
	else {
		rec = get_critconfig(NULL, CRITCONF_FIRST, NULL);
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
	critconf_t *rec = NULL;

	if (!hostname || !service) {
		setstatus("NOT SAVED: enter both a host and a test%s", "");
	}
	else {
		SBUF_DEFINE(key);
		char *realkey;
		char datestr[20];
		time_t now = getcurrenttime(NULL);
		size_t upd_buflen;

		SBUF_MALLOC(key, strlen(hostname) + strlen(service) + 2);
		strftime(datestr, sizeof(datestr), "%Y-%m-%d %H:%M:%S", localtime(&now));
		snprintf(key, key_buflen, "%s|%s", hostname, service);
		rec = get_critconfig(key, CRITCONF_FIRSTMATCH, &realkey);
		if (rec == NULL) {
			rec = (critconf_t *)calloc(1, sizeof(critconf_t));
			rec->key = strdup(key);
		}
		rec->priority = rq_priority;
		rec->starttime = (rq_start > 0) ? rq_start : 0;
		rec->endtime = (rq_end > 0) ? rq_end : 0;

		if (rec->crittime) {
			xfree(rec->crittime); rec->crittime = NULL;
		}

		if (rq_crittime) {
			rec->crittime = (strcmp(rq_crittime, "*:0000:2400") == 0) ? NULL : strdup(rq_crittime);
		}

		if (rec->ttgroup) xfree(rec->ttgroup); 
		rec->ttgroup = (rq_group ? strdup(rq_group) : NULL);
		if (rec->ttextra) xfree(rec->ttextra); 
		rec->ttextra = (rq_extra ? strdup(rq_extra) : NULL);
		if (rec->updinfo) xfree(rec->updinfo);
		upd_buflen = strlen(operator) + strlen(datestr) + 2;
		rec->updinfo = (char *)malloc(upd_buflen);
		snprintf(rec->updinfo, upd_buflen, "%s %s", operator, datestr);

		setsaveresult(update_critconfig(rec), "Saved");
		xfree(key);
	}

	findrecord(hostname, service, NULL, NULL, NULL);
}

void addclone(char *origin, char *newhosts, char *service)
{
	char *newclone;

	if (!origin) {
		/* A clone points at a host's record: without the host there is none */
		findrecord(NULL, NULL, "Nothing cloned: enter the host to clone from", NULL, NULL);
		return;
	}
	if (!newhosts) {
		/* Without hosts to add, strtok() would resume cgi_request()'s freed buffer */
		findrecord(NULL, NULL, "Nothing cloned: enter the hosts to clone to", NULL, NULL);
		return;
	}

	newclone = strtok(newhosts, " ");
	while (newclone) {
		addclone_critconfig(origin, newclone);
		newclone = strtok(NULL, " ");
	}

	setsaveresult(update_critconfig(NULL), "Saved");
	findrecord(origin, service, NULL, NULL, NULL);
}

void dropclone(char *origin, char *drops, char *service)
{
	char *drop;

	drop = strtok(drops, " ");
	while (drop) {
		dropclone_critconfig(drop);
		drop = strtok(NULL, " ");
	}

	setsaveresult(update_critconfig(NULL), "Saved");
	findrecord(origin, service, NULL, NULL, NULL);
}

void deleterecord(char *hostname, char *service, int evenifcloned)
{
	SBUF_DEFINE(key);
	char *realkey = NULL;
	char why[1024];

	if (!hostname || !service) {
		/* Both name a record; without them there is nothing to delete */
		findrecord(hostname, service, "Nothing deleted: enter both a host and a test", NULL, NULL);
		return;
	}

	SBUF_MALLOC(key, strlen(hostname) + strlen(service) + 2);
	snprintf(key, key_buflen, "%s|%s", hostname, service);
	if (get_critconfig(key, CRITCONF_FIRSTMATCH, &realkey) && realkey && (strcmp(realkey, key) != 0)) {
		/*
		 * A clone: the host has no record of its own, it shows its origin's.
		 * Deleting by this key would find nothing, and say "Deleted".
		 */
		char *origin = strdup(realkey), *p = strchr(origin, '|');

		if (p) *p = '\0';
		snprintf(why, sizeof(why), "Nothing deleted: %s is a clone of ", htmlattrquoted(hostname));
		strncat(why, htmlattrquoted(origin), sizeof(why) - strlen(why) - 1);
		strncat(why, ". Remove it from that host's clone list instead", sizeof(why) - strlen(why) - 1);
		snprintf(statusmsg, sizeof(statusmsg), "%s", why);
		xfree(origin);
	}
	else switch (delete_critconfig(key, evenifcloned)) {
	  case CRITDELETE_OK:
		setsaveresult(update_critconfig(NULL), "Deleted");
		break;

	  case CRITDELETE_NORECORD:
		setstatus("Nothing deleted: there is no record for this host and test%s", "");
		break;

	  case CRITDELETE_CLONED:
	  default:
		/* It has clones: the form says "Will not delete record that is cloned" */
		break;
	}
	xfree(key);

	/* Not "Will not delete record that is cloned" where the status already says why not */
	findrecord(hostname, service, NULL, NULL, 
		   (evenifcloned ? "Warning: Orphans will be ignored" : (*statusmsg ? NULL : "Will not delete record that is cloned")));
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

	redirect_cgilog("criticaleditor");

        /* We only want to accept posts from certain pages */
	if (cgi_ispost()) {
		char cgisource[1024]; char *p;
		p = csp_header("criticaleditor"); if (p) fprintf(stdout, "%s", p);
		snprintf(cgisource, sizeof(cgisource), "%s/%s", xgetenv("SECURECGIBINURL"), "criticaleditor");
		if (!cgi_refererok(cgisource)) {
			fprintf(stdout, "Location: %s.sh?\n\n", cgisource);
			return 0;
		}
	}

	parse_query();
	load_critconfig(configfn);

	switch (editaction) {
	  case CRITEDIT_FIND:
		findrecord(rq_hostname, rq_service, 
			   ((rq_hostname && rq_service) ? "No record for this host/service" : NULL),
			   "Cloned - showing master record", NULL);
		break;

	  case CRITEDIT_NEXT:
		nextrecord(rq_hostname, rq_service, "Cloned - showing master record", NULL);
		break;

	  case CRITEDIT_UPDATE:
		updaterecord(rq_hostname, rq_service);
		break;

	  case CRITEDIT_DELETE:
		deleterecord(rq_hostname, rq_service, rq_dropevenifcloned);
		break;

	  case CRITEDIT_ADDCLONE:
		addclone(rq_hostname, rq_clonestoadd, rq_service);
		break;

	  case CRITEDIT_DROPCLONE:
		dropclone(rq_hostname, rq_clonestodrop, rq_service);
		break;
	}

	return 0;
}

