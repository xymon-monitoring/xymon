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
 * What the last action did, shown above the form: "Saved", or why it was
 * not. It takes the place of findrecord()'s own warnings, so a failed save
 * can never look like a successful one.
 */
static char statusmsg[4096] = "";

static void setstatus(const char *fmt, const char *arg)
{
	/* arg is what the user typed, or a system message: escaped for HTML */
	snprintf(statusmsg, sizeof(statusmsg), fmt, (arg ? htmlquoted((char *)arg) : ""));
}

/* After update_critconfig() has failed: say which file, why, and as whom */
static void setsavefailed(void)
{
	char why[1024];
	int e = errno;		/* before getpwuid(), which may change it */
	struct passwd *pw = getpwuid(geteuid());

	snprintf(why, sizeof(why), "%s. The web server runs as %s, which must be able to write critical.cfg and critical.cfg.bak",
		 strerror(e), (pw ? pw->pw_name : "an unknown user"));
	setstatus("NOT SAVED: cannot write critical.cfg: %s", why);
}

/*
 * A host must be in hosts.cfg: a record for a name that isn't there never
 * matches anything. If hosts.cfg could not be loaded at all, nothing is
 * refused on that account.
 */
static int hosts_loaded = 0;

static int known_host(const char *hostname)
{
	return (!hosts_loaded || (hostinfo((char *)hostname) != NULL));
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
		 * The form always sends all three; a request without one of them
		 * gets the form's defaults (every day, all day) instead of a crash.
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
	if (*statusmsg) snprintf(warnmsg, sizeof(warnmsg), "%s", statusmsg);

	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	showform(stdout, "critedit", "critedit_form", COL_BLUE, getcurrenttime(NULL), warnmsg, NULL);
}


void nextrecord(char *hostname, char *service, char *isclonewarning, char *hascloneswarning)
{
	critconf_t *rec = NULL;
	char *nexthost, *nextservice;

	if (hostname) {
		/*
		 * The configured record after the one typed in. That record need
		 * not exist: for a host and test with no record yet, it is the
		 * first one that sorts after them, as the records are sorted by
		 * key ("host|test"). A host alone, or the start of one, finds its
		 * first record: "mail" finds "mail.example.com|conn".
		 */
		SBUF_DEFINE(key);

		SBUF_MALLOC(key, strlen(hostname) + (service ? strlen(service) : 0) + 2);
		if (service) snprintf(key, key_buflen, "%s|%s", hostname, service);
		else snprintf(key, key_buflen, "%s", hostname);

		if (service && get_critconfig(key, CRITCONF_FIRSTMATCH, NULL)) {
			rec = get_critconfig(NULL, CRITCONF_NEXT, NULL);
		}
		else {
			for (rec = get_critconfig(NULL, CRITCONF_FIRST, NULL);
			     (rec && ((service ? (strcmp(rec->key, key) <= 0) : (strcmp(rec->key, key) < 0))));
			     rec = get_critconfig(NULL, CRITCONF_NEXT, NULL)) ;
		}
		xfree(key);

		if (!rec) {
			/* Nothing after it: stay on what was typed, and say so */
			setstatus("No configured critical system after this one%s", "");
			findrecord(hostname, service, NULL, isclonewarning, hascloneswarning);
			return;
		}
	}
	else {
		rec = get_critconfig(NULL, CRITCONF_FIRST, NULL);
		if (!rec) {
			setstatus("No critical systems are configured yet%s", "");
			findrecord(NULL, NULL, NULL, NULL, NULL);
			return;
		}
	}

	nexthost = strdup(rec->key);
	nextservice = strchr(nexthost, '|'); if (nextservice) { *nextservice = '\0'; nextservice++; }

	findrecord(nexthost, nextservice, NULL, isclonewarning, hascloneswarning);
	xfree(nexthost);
}

void updaterecord(char *hostname, char *service)
{
	critconf_t *rec = NULL;

	if (!hostname || !service) {
		setstatus("NOT SAVED: enter both a host and a test%s", "");
	}
	else if (!known_host(hostname)) {
		setstatus("NOT SAVED: there is no host %s in hosts.cfg", hostname);
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

		if (update_critconfig(rec) == 0) setstatus("Saved%s", "");
		else setsavefailed();
		xfree(key);
	}

	findrecord(hostname, service, NULL, NULL, NULL);
}

void addclone(char *origin, char *newhosts, char *service)
{
	char *newclone;

	char *unknown = NULL;

	newclone = strtok(newhosts, " ");
	while (newclone) {
		if (known_host(newclone)) addclone_critconfig(origin, newclone);
		else if (!unknown) unknown = newclone;
		newclone = strtok(NULL, " ");
	}

	if (update_critconfig(NULL) != 0) setsavefailed();
	else if (unknown) setstatus("Saved, except: there is no host %s in hosts.cfg", unknown);
	else setstatus("Saved%s", "");
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

	if (update_critconfig(NULL) == 0) setstatus("Saved%s", "");
	else setsavefailed();
	findrecord(origin, service, NULL, NULL, NULL);
}

void deleterecord(char *hostname, char *service, int evenifcloned)
{
	SBUF_DEFINE(key);

	if (!hostname || !service) {
		/* Both are needed to name a record; without them the key can't be built */
		setstatus("Nothing deleted: enter both a host and a test%s", "");
		findrecord(hostname, service, NULL, NULL, NULL);
		return;
	}

	SBUF_MALLOC(key, strlen(hostname) + strlen(service) + 2);
	snprintf(key, key_buflen, "%s|%s", hostname, service);
	if (!get_critconfig(key, CRITCONF_FIRSTMATCH, NULL)) {
		setstatus("Nothing deleted: there is no record for this host and test%s", "");
	}
	else if (delete_critconfig(key, evenifcloned) == 0) {
		if (update_critconfig(NULL) == 0) setstatus("Deleted%s", "");
		else setsavefailed();
	}
	xfree(key);

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
	hosts_loaded = (load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn()) == 0) && (first_host() != NULL);

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

