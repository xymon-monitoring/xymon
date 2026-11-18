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
static char *rq_prevhost = NULL;	/* the host the form was showing */
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
		else if (strcasecmp(cwalk->name, "PREVHOST") == 0) {
			if (*cwalk->value) rq_prevhost = strdup(cwalk->value);
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

/* ---- What can be chosen, and what is configured: shown with every form ---- */

static int cmpstr(const void *a, const void *b)
{
	return strcmp(*(char * const *)a, *(char * const *)b);
}

/* "W:0700:2000" as "Mon-Fri 07:00-20:00"; no time set is "Always" */
static void addwhen(strbuffer_t *buf, char *crittime)
{
	static const char *daynames[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	char days[64], start[5], end[5];
	char *p;

	if (!crittime || !*crittime || (strcmp(crittime, "*:0000:2400") == 0)) {
		addtobuffer(buf, "Always");
		return;
	}
	if ((sscanf(crittime, "%63[^:]:%4[0-9]:%4[0-9]", days, start, end) != 3) || (strlen(start) != 4) || (strlen(end) != 4)) {
		addtobuffer(buf, htmlattrquoted(crittime));	/* not a form the editor writes: shown as it is */
		return;
	}

	if (strcmp(days, "*") == 0) addtobuffer(buf, "All days");
	else if (strcmp(days, "W") == 0) addtobuffer(buf, "Mon-Fri");
	else {
		int first = 1;
		for (p = days; (*p); p++) {
			if ((*p < '0') || (*p > '6')) continue;
			if (!first) addtobuffer(buf, ",");
			addtobuffer(buf, (char *)daynames[*p - '0']);
			first = 0;
		}
	}
	addtobuffer(buf, " ");
	addtobufferraw(buf, start, 2); addtobuffer(buf, ":"); addtobufferraw(buf, start+2, 2);
	addtobuffer(buf, "-");
	addtobufferraw(buf, end, 2); addtobuffer(buf, ":"); addtobufferraw(buf, end+2, 2);
}

/* An <option> for a <datalist> */
static void addoption(strbuffer_t *buf, char *value)
{
	addtobuffer_many(buf, "<option value=\"", htmlattrquoted(value), "\">\n", NULL);
}

/*
 * The host: typed, or picked from the hosts in hosts.cfg, sorted. Typed, as
 * a template for clones ("Standard Unix") is a name no host has.
 */
static char *host_field(char *hostname)
{
	strbuffer_t *buf = newstrbuffer(0);
	char **names = NULL;
	int count = 0, i, known = 0;
	void *h;

	for (h = first_host(); (h); h = next_host(h, 0)) {
		char **more = (char **)realloc(names, (count + 1) * sizeof(char *));

		if (!more) break;	/* out of memory: offer the names so far */
		names = more;
		names[count++] = xmh_item(h, XMH_HOSTNAME);
	}
	if (count) qsort(names, count, sizeof(char *), cmpstr);

	addtobuffer_many(buf, "<INPUT NAME=\"HOSTNAME\" TYPE=\"TEXT\" SIZE=30 LIST=\"critedit-hosts\" onChange=\"this.form.submit()\" VALUE=\"",
			 htmlattrquoted(hostname), "\">\n<DATALIST ID=\"critedit-hosts\">\n", NULL);
	for (i = 0; (i < count); i++) {
		addoption(buf, names[i]);
		if (hostname && (strcmp(names[i], hostname) == 0)) known = 1;
	}
	addtobuffer(buf, "</DATALIST>");
	if (count && hostname && *hostname && !known) {
		/* With no hosts.cfg loaded, every name would be marked */
		addtobuffer(buf, "<BR><FONT SIZE=\"-1\">Not a host in hosts.cfg: a template for clones, or a host since removed.</FONT>");
	}

	xfree(names);
	return grabstrbuffer(buf);
}

/*
 * The tests the host reports now, from xymond's board, sorted. The columns
 * xymond makes up itself (info, trends, client data) are left out: they
 * never change color. The board is asked by pattern, so only rows whose
 * host name matches exactly are taken.
 */
static int board_tests(char *hostname, char ***tests)
{
	SBUF_DEFINE(req);
	sendreturn_t *sres;
	char *resp, *line, *bar;
	char **more;
	int count = 0;

	*tests = NULL;
	if (!hostname || !*hostname || cgi_hasctrl(hostname)) return 0;

	SBUF_MALLOC(req, strlen(hostname) + 64);
	snprintf(req, req_buflen, "xymondboard host=^%s$ fields=hostname,testname", hostname);
	sres = newsendreturnbuf(1, NULL);
	if (sendmessage(req, NULL, XYMON_TIMEOUT, sres) == XYMONSEND_OK) {
		resp = getsendreturnstr(sres, 1);
		for (line = strtok(resp, "\n"); (line); line = strtok(NULL, "\n")) {
			bar = strchr(line, '|');
			if (!bar) continue;
			*bar = '\0';
			if (strcmp(line, hostname) != 0) continue;
			bar++;
			if (!*bar || (strcmp(bar, xgetenv("INFOCOLUMN")) == 0) ||
			    (strcmp(bar, xgetenv("TRENDSCOLUMN")) == 0) ||
			    (strcmp(bar, xgetenv("CLIENTCOLUMN")) == 0)) continue;
			more = (char **)realloc(*tests, (count + 1) * sizeof(char *));
			if (!more) break;	/* out of memory: offer the tests so far */
			*tests = more;
			(*tests)[count++] = strdup(bar);
		}
		xfree(resp);
	}
	freesendreturnbuf(sres);
	xfree(req);

	if (count) qsort(*tests, count, sizeof(char *), cmpstr);
	return count;
}

/* The test: typed, or picked from the tests the host reports now */
static char *service_field(char *hostname, char *service)
{
	strbuffer_t *buf = newstrbuffer(0);
	char **tests;
	int count, i;

	/* So that choosing another host starts with no test chosen */
	if (hostname && *hostname) {
		addtobuffer_many(buf, "<INPUT TYPE=\"HIDDEN\" NAME=\"PREVHOST\" VALUE=\"", htmlattrquoted(hostname), "\">", NULL);
	}

	addtobuffer_many(buf, "<INPUT NAME=\"SERVICE\" TYPE=\"TEXT\" SIZE=12 LIST=\"critedit-tests\" VALUE=\"",
			 htmlattrquoted(service), "\">\n<DATALIST ID=\"critedit-tests\">\n", NULL);
	count = board_tests(hostname, &tests);
	for (i = 0; (i < count); i++) {
		addoption(buf, tests[i]);
		xfree(tests[i]);
	}
	if (tests) xfree(tests);
	addtobuffer(buf, "</DATALIST>");

	return grabstrbuffer(buf);
}

/* One row per configured critical system, each with a link to edit it */
static char *record_rows(char *hostname, char *service)
{
	strbuffer_t *buf = newstrbuffer(0);
	critconf_t *rec;
	char prio[16];
	int count = 0;

	for (rec = get_critconfig(NULL, CRITCONF_FIRST, NULL); (rec); rec = get_critconfig(NULL, CRITCONF_NEXT, NULL)) {
		char *host = strdup(rec->key), *svc, *hostenc;
		int current;

		svc = strchr(host, '|'); if (svc) *(svc++) = '\0'; else svc = "";
		current = (hostname && service && (strcmp(host, hostname) == 0) && (strcmp(svc, service) == 0));

		hostenc = strdup(urlencode(host));
		addtobuffer(buf, current ? "<TR BGCOLOR=\"#000066\">" : "<TR>");
		addtobuffer_many(buf, "<TD><A HREF=\"criticaleditor.sh?HOSTNAME=", hostenc,
				 "&amp;SERVICE=", urlencode(svc), "&amp;Find=Show\">", NULL);
		addtobuffer(buf, htmlattrquoted(host));
		addtobuffer_many(buf, "</A></TD><TD>", htmlattrquoted(svc), NULL);
		snprintf(prio, sizeof(prio), "%d", rec->priority);
		addtobuffer_many(buf, "</TD><TD ALIGN=CENTER>", prio, "</TD><TD>", NULL);
		addwhen(buf, rec->crittime);
		addtobuffer_many(buf, "</TD><TD>", htmlattrquoted(rec->ttgroup), "</TD></TR>\n", NULL);

		xfree(hostenc);
		xfree(host);
		count++;
	}

	if (count == 0) {
		addtobuffer(buf, "<TR><TD COLSPAN=5><I>None yet: choose a host and a test below, fill in the form and click Update record.</I></TD></TR>\n");
	}

	return grabstrbuffer(buf);
}

static void setlists(char *hostname, char *service)
{
	char *hostf = host_field(hostname);
	char *svcf = service_field(hostname, service);
	char *rows = record_rows(hostname, service);

	sethostenv_critedit_lists(hostf, svcf, rows);
	xfree(hostf); xfree(svcf); xfree(rows);
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

	setlists(hostname, service);

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
	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());	/* for the host list */

	/* Another host was chosen: the test chosen for the last one does not apply */
	if ((editaction == CRITEDIT_FIND) && rq_prevhost && rq_hostname && (strcmp(rq_prevhost, rq_hostname) != 0))
		rq_service = NULL;

	switch (editaction) {
	  case CRITEDIT_FIND:
		findrecord(rq_hostname, rq_service, 
			   ((rq_hostname && rq_service) ? "Not on the Critical Systems view yet: fill in the form and click Update record to add it" : NULL),
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

