/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for handling header- and footer-files.                */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: headfoot.c,v 1.53 2006-07-20 16:06:41 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <pcre.h>

#include "libbbgen.h"
#include "version.h"

/* Stuff for headfoot - variables we can set dynamically */
static char *hostenv_hikey = NULL;
static char *hostenv_host = NULL;
static char *hostenv_ip = NULL;
static char *hostenv_svc = NULL;
static char *hostenv_color = NULL;
static char *hostenv_pagepath = NULL;

static time_t hostenv_reportstart = 0;
static time_t hostenv_reportend = 0;

static char *hostenv_repwarn = NULL;
static char *hostenv_reppanic = NULL;

static time_t hostenv_snapshot = 0;
static char *hostenv_logtime = NULL;
static char *hostenv_templatedir = NULL;
static int hostenv_refresh = 60;

static char *statusboard = NULL;
static char *scheduleboard = NULL;

static char *hostpattern_text = NULL;
static pcre *hostpattern = NULL;
static char *pagepattern_text = NULL;
static pcre *pagepattern = NULL;
static char *ippattern_text = NULL;
static pcre *ippattern = NULL;
static RbtHandle hostnames;
static RbtHandle testnames;

typedef struct treerec_t {
	char *name;
	int flag;
} treerec_t;

static void clearflags(RbtHandle tree)
{
	RbtIterator handle;
	treerec_t *rec;

	if (!tree) return;

	for (handle = rbtBegin(tree); (handle != rbtEnd(tree)); handle = rbtNext(tree, handle)) {
		rec = (treerec_t *)gettreeitem(tree, handle);
		rec->flag = 0;
	}
}

void sethostenv(char *host, char *ip, char *svc, char *color, char *hikey)
{
	if (hostenv_hikey) xfree(hostenv_hikey);
	if (hostenv_host)  xfree(hostenv_host);
	if (hostenv_ip)    xfree(hostenv_ip);
	if (hostenv_svc)   xfree(hostenv_svc);
	if (hostenv_color) xfree(hostenv_color);

	hostenv_hikey = (hikey ? strdup(hikey) : NULL);
	hostenv_host = strdup(host);
	hostenv_ip = strdup(ip);
	hostenv_svc = strdup(svc);
	hostenv_color = strdup(color);
}

void sethostenv_report(time_t reportstart, time_t reportend, double repwarn, double reppanic)
{
	if (hostenv_repwarn == NULL) hostenv_repwarn = malloc(10);
	if (hostenv_reppanic == NULL) hostenv_reppanic = malloc(10);

	hostenv_reportstart = reportstart;
	hostenv_reportend = reportend;

	sprintf(hostenv_repwarn, "%.2f", repwarn);
	sprintf(hostenv_reppanic, "%.2f", reppanic);
}

void sethostenv_snapshot(time_t snapshot)
{
	hostenv_snapshot = snapshot;
}

void sethostenv_histlog(char *histtime)
{
	if (hostenv_logtime) xfree(hostenv_logtime);
	hostenv_logtime = strdup(histtime);
}

void sethostenv_template(char *dir)
{
	if (hostenv_templatedir) xfree(hostenv_templatedir);
	hostenv_templatedir = strdup(dir);
}

void sethostenv_refresh(int n)
{
	hostenv_refresh = n;
}

void sethostenv_filter(char *hostptn, char *pageptn, char *ipptn)
{
	const char *errmsg;
	int errofs;

	if (hostpattern_text) xfree(hostpattern_text);
	if (hostpattern) { pcre_free(hostpattern); hostpattern = NULL; }
	if (pagepattern_text) xfree(pagepattern_text);
	if (pagepattern) { pcre_free(pagepattern); pagepattern = NULL; }
	if (ippattern_text) xfree(ippattern_text);
	if (ippattern) { pcre_free(ippattern); ippattern = NULL; }

	/* Setup the pattern to match names against */
	if (hostptn) {
		hostpattern_text = strdup(hostptn);
		hostpattern = pcre_compile(hostptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
	}
	if (pageptn) {
		pagepattern_text = strdup(pageptn);
		pagepattern = pcre_compile(pageptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
	}
	if (ipptn) {
		ippattern_text = strdup(ipptn);
		ippattern = pcre_compile(ipptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
	}
}

static int nkackttprio = 0;
static char *nkackttgroup = NULL;
static char *nkackttextra = NULL;
static char *nkackinfourl = NULL;
static char *nkackdocurl = NULL;

void sethostenv_nkack(int nkprio, char *nkttgroup, char *nkttextra, char *infourl, char *docurl)
{
	nkackttprio = nkprio;
	if (nkackttgroup) xfree(nkackttgroup); nkackttgroup = strdup((nkttgroup && *nkttgroup) ? nkttgroup : "&nbsp;");
	if (nkackttextra) xfree(nkackttextra); nkackttextra = strdup((nkttextra && *nkttextra) ? nkttextra : "&nbsp;");
	if (nkackinfourl) xfree(nkackinfourl); nkackinfourl = strdup(infourl);
	if (nkackdocurl) xfree(nkackdocurl); nkackdocurl = strdup((docurl && *docurl) ? docurl : "");
}

static char *nkeditupdinfo = NULL;
static int nkeditprio = -1;
static char *nkeditgroup = NULL;
static time_t nkeditstarttime = 0;
static time_t nkeditendtime = 0;
static char *nkeditextra = NULL;
static char *nkeditslawkdays = NULL;
static char *nkeditslastart = NULL;
static char *nkeditslaend = NULL;
static char **nkeditclonelist = NULL;
static int nkeditclonesize = 0;

void sethostenv_nkedit(char *updinfo, int prio, char *group, time_t starttime, time_t endtime, char *nktime, char *extra)
{
	char *p;

	if (nkeditupdinfo) xfree(nkeditupdinfo);
	nkeditupdinfo = strdup(updinfo);

	nkeditprio = prio;
	nkeditstarttime = starttime;
	nkeditendtime = endtime;

	if (nkeditgroup) xfree(nkeditgroup);
	nkeditgroup = strdup(group ? group : "");

	if (nkeditextra) xfree(nkeditextra);
	nkeditextra = strdup(extra ? extra : "");

	if (nkeditslawkdays) xfree(nkeditslawkdays);
	nkeditslawkdays = nkeditslastart = nkeditslaend = NULL;

	if (nktime) {
		nkeditslawkdays = strdup(nktime);
		p = strchr(nkeditslawkdays, ':');
		if (p) {
			*p = '\0';
			nkeditslastart = p+1;

			p = strchr(nkeditslastart, ':');
			if (p) {
				*p = '\0';
				nkeditslaend = p+1;
			}
		}

		if (nkeditslawkdays && (!nkeditslastart || !nkeditslaend)) {
			xfree(nkeditslawkdays);
			nkeditslawkdays = nkeditslastart = nkeditslaend = NULL;
		}
	}
}

void sethostenv_nkclonelist_clear(void)
{
	int i;

	if (nkeditclonelist) {
		for (i=0; (nkeditclonelist[i]); i++) xfree(nkeditclonelist[i]);
		xfree(nkeditclonelist);
	}
	nkeditclonelist = malloc(sizeof(char *));
	nkeditclonelist[0] = NULL;
	nkeditclonesize = 0;
}

void sethostenv_nkclonelist_add(char *hostname)
{
	char *p;

	nkeditclonelist = (char **)realloc(nkeditclonelist, (nkeditclonesize + 2)*sizeof(char *));
	nkeditclonelist[nkeditclonesize] = strdup(hostname);
	p = nkeditclonelist[nkeditclonesize];
	nkeditclonelist[++nkeditclonesize] = NULL;

	p += (strlen(p) - 1);
	if (*p == '=') *p = '\0';
}


char *wkdayselect(char wkday, char *valtxt, int isdefault)
{
	static char result[100];
	char *selstr;

	if (!nkeditslawkdays) {
		if (isdefault) selstr = "SELECTED";
		else selstr = "";
	}
	else {
		if (strchr(nkeditslawkdays, wkday)) selstr = "SELECTED";
		else selstr = "";
	}

	sprintf(result, "<option value=\"%c\" %s>%s</option>\n", wkday, selstr, valtxt);

	return result;
}


static namelist_t *wanted_host(char *hostname)
{
	namelist_t *hinfo = hostinfo(hostname);
	int result, ovector[30];

	if (!hinfo) return NULL;

	if (hostpattern) {
		result = pcre_exec(hostpattern, NULL, hostname, strlen(hostname), 0, 0,
				ovector, (sizeof(ovector)/sizeof(int)));
		if (result < 0) return NULL;
	}

	if (pagepattern && hinfo) {
		char *pname = bbh_item(hinfo, BBH_PAGEPATH);
		result = pcre_exec(pagepattern, NULL, pname, strlen(pname), 0, 0,
				ovector, (sizeof(ovector)/sizeof(int)));
		if (result < 0) return NULL;
	}

	if (ippattern && hinfo) {
		char *hostip = bbh_item(hinfo, BBH_IP);
		result = pcre_exec(ippattern, NULL, hostip, strlen(hostip), 0, 0,
				ovector, (sizeof(ovector)/sizeof(int)));
		if (result < 0) return NULL;
	}

	return hinfo;
}


static void fetch_board(void)
{
	static int haveboard = 0;
	char *walk, *eoln;

	if (haveboard) return;

	if (sendmessage("hobbitdboard fields=hostname,testname,disabletime,dismsg", 
			NULL, NULL, &statusboard, 1, BBTALK_TIMEOUT) != BB_OK)
		return;

	haveboard = 1;

	hostnames = rbtNew(name_compare);
	testnames = rbtNew(name_compare);
	walk = statusboard;
	while (walk) {
		eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
		if (strlen(walk) && (strncmp(walk, "summary|", 8) != 0) && (strncmp(walk, "dialup|", 7) != 0)) {
			char *buf, *hname = NULL, *tname = NULL;
			treerec_t *newrec;

			buf = strdup(walk);

			hname = gettok(buf, "|");

			if (hname && wanted_host(hname)) {
				newrec = (treerec_t *)malloc(sizeof(treerec_t));
				newrec->name = strdup(hname);
				newrec->flag = 0;
				rbtInsert(hostnames, newrec->name, newrec);

				tname = gettok(NULL, "|");
				if (tname) {
					newrec = (treerec_t *)malloc(sizeof(treerec_t));
					newrec->name = strdup(tname);
					newrec->flag = 0;
					rbtInsert(testnames, strdup(tname), newrec);
				}
			}

			xfree(buf);
		}

		if (eoln) {
			*eoln = '\n';
			walk = eoln + 1;
		}
		else
			walk = NULL;
	}

	if (sendmessage("schedule", NULL, NULL, &scheduleboard, 1, BBTALK_TIMEOUT) != BB_OK)
		return;
}


typedef struct distest_t {
	char *name;
	char *cause;
	time_t until;
	struct distest_t *next;
} distest_t;

typedef struct dishost_t {
	char *name;
	struct distest_t *tests;
	struct dishost_t *next;
} dishost_t;

void output_parsed(FILE *output, char *templatedata, int bgcolor, time_t selectedtime)
{
	char	*t_start, *t_next;
	char	savechar;
	time_t	now = time(NULL);
	time_t  yesterday = time(NULL) - 86400;
	struct  tm *nowtm;

	for (t_start = templatedata, t_next = strchr(t_start, '&'); (t_next); ) {
		/* Copy from t_start to t_next unchanged */
		*t_next = '\0'; t_next++;
		fprintf(output, "%s", t_start);

		/* Find token */
		t_start = t_next;
		/* Dont include lower-case letters - reserve those for eg "&nbsp;" */
		t_next += strspn(t_next, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
		savechar = *t_next; *t_next = '\0';

		if (strcmp(t_start, "BBDATE") == 0) {
			char *bbdatefmt = xgetenv("BBDATEFORMAT");
			char datestr[100];

			MEMDEFINE(datestr);

			/*
			 * If no BBDATEFORMAT setting, use a format string that
			 * produces output similar to that from ctime()
			 */
			if (bbdatefmt == NULL) bbdatefmt = "%a %b %d %H:%M:%S %Y\n";

			if (hostenv_reportstart != 0) {
				char starttime[20], endtime[20];

				MEMDEFINE(starttime); MEMDEFINE(endtime);

				strftime(starttime, sizeof(starttime), "%b %d %Y", localtime(&hostenv_reportstart));
				strftime(endtime, sizeof(endtime), "%b %d %Y", localtime(&hostenv_reportend));
				if (strcmp(starttime, endtime) == 0)
					fprintf(output, "%s", starttime);
				else
					fprintf(output, "%s - %s", starttime, endtime);

				MEMUNDEFINE(starttime); MEMUNDEFINE(endtime);
			}
			else if (hostenv_snapshot != 0) {
				strftime(datestr, sizeof(datestr), bbdatefmt, localtime(&hostenv_snapshot));
				fprintf(output, "%s", datestr);
			}
			else {
				strftime(datestr, sizeof(datestr), bbdatefmt, localtime(&now));
				fprintf(output, "%s", datestr);
			}

			MEMUNDEFINE(datestr);
		}

		else if (strcmp(t_start, "BBBACKGROUND") == 0)  {
			fprintf(output, "%s", colorname(bgcolor));
		}
		else if (strcmp(t_start, "BBCOLOR") == 0)       fprintf(output, "%s", hostenv_color);
		else if (strcmp(t_start, "BBSVC") == 0)         fprintf(output, "%s", hostenv_svc);
		else if (strcmp(t_start, "BBHOST") == 0)        fprintf(output, "%s", hostenv_host);
		else if (strcmp(t_start, "BBHIKEY") == 0)       fprintf(output, "%s", (hostenv_hikey ? hostenv_hikey : hostenv_host));
		else if (strcmp(t_start, "BBIP") == 0)          fprintf(output, "%s", hostenv_ip);
		else if (strcmp(t_start, "BBIPNAME") == 0) {
			if (strcmp(hostenv_ip, "0.0.0.0") == 0) fprintf(output, "%s", hostenv_host);
			else fprintf(output, "%s", hostenv_ip);
		}
		else if (strcmp(t_start, "BBREPWARN") == 0)     fprintf(output, "%s", hostenv_repwarn);
		else if (strcmp(t_start, "BBREPPANIC") == 0)    fprintf(output, "%s", hostenv_reppanic);
		else if (strcmp(t_start, "LOGTIME") == 0) 	fprintf(output, "%s", (hostenv_logtime ? hostenv_logtime : ""));
		else if (strcmp(t_start, "BBREFRESH") == 0)     fprintf(output, "%d", hostenv_refresh);
		else if (strcmp(t_start, "BBPAGEPATH") == 0)    fprintf(output, "%s", (hostenv_pagepath ? hostenv_pagepath : ""));

		else if (strcmp(t_start, "REPMONLIST") == 0) {
			int i;
			struct tm monthtm;
			char mname[20];
			char *selstr;

			MEMDEFINE(mname);

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 12); i++) {
				if (i == (nowtm->tm_mon + 1)) selstr = "SELECTED"; else selstr = "";
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%s\n", i, selstr, mname);
			}

			MEMUNDEFINE(mname);
		}
		else if (strcmp(t_start, "MONLIST") == 0) {
			int i;
			struct tm monthtm;
			char mname[20];

			MEMDEFINE(mname);

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 12); i++) {
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<OPTION VALUE=\"%d\">%s\n", i, mname);
			}

			MEMUNDEFINE(mname);
		}
		else if (strcmp(t_start, "REPWEEKLIST") == 0) {
			int i;
			char weekstr[5];
			int weeknum;
			char *selstr;

			nowtm = localtime(&selectedtime);
			strftime(weekstr, sizeof(weekstr)-1, "%V", nowtm); weeknum = atoi(weekstr);
			for (i=1; (i <= 53); i++) {
				if (i == weeknum) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "REPDAYLIST") == 0) {
			int i;
			char *selstr;

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 31); i++) {
				if (i == nowtm->tm_mday) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "DAYLIST") == 0) {
			int i;

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 31); i++) {
				fprintf(output, "<OPTION VALUE=\"%d\">%d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPYEARLIST") == 0) {
			int i;
			char *selstr;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900 - 5;
			endyear = nowtm->tm_year + 1900;

			for (i=beginyear; (i <= endyear); i++) {
				if (i == (nowtm->tm_year + 1900)) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "FUTUREYEARLIST") == 0) {
			int i;
			char *selstr;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900;
			endyear = nowtm->tm_year + 1900 + 5;

			for (i=beginyear; (i <= endyear); i++) {
				if (i == (nowtm->tm_year + 1900)) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "YEARLIST") == 0) {
			int i;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900;
			endyear = nowtm->tm_year + 1900 + 5;

			for (i=beginyear; (i <= endyear); i++) {
				fprintf(output, "<OPTION VALUE=\"%d\">%d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPHOURLIST") == 0) { 
			int i; 
			struct tm *nowtm = localtime(&yesterday); 
			char *selstr;

			for (i=0; (i <= 24); i++) {
				if (i == nowtm->tm_hour) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "HOURLIST") == 0) { 
			int i; 

			for (i=0; (i <= 24); i++) {
				fprintf(output, "<OPTION VALUE=\"%d\">%d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPMINLIST") == 0) {
			int i;
			struct tm *nowtm = localtime(&yesterday);
			char *selstr;

			for (i=0; (i <= 59); i++) {
				if (i == nowtm->tm_min) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%02d\" %s>%02d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "MINLIST") == 0) {
			int i;

			for (i=0; (i <= 59); i++) {
				fprintf(output, "<OPTION VALUE=\"%02d\">%02d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPSECLIST") == 0) {
			int i;
			char *selstr;

			for (i=0; (i <= 59); i++) {
				if (i == 0) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%02d\" %s>%02d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "HOSTFILTER") == 0) {
			if (hostpattern_text) fprintf(output, "%s", hostpattern_text);
		}
		else if (strcmp(t_start, "PAGEFILTER") == 0) {
			if (pagepattern_text) fprintf(output, "%s", pagepattern_text);
		}
		else if (strcmp(t_start, "IPFILTER") == 0) {
			if (ippattern_text) fprintf(output, "%s", ippattern_text);
		}
		else if (strcmp(t_start, "HOSTLIST") == 0) {
			RbtIterator handle;
			treerec_t *rec;

			fetch_board();

			for (handle = rbtBegin(hostnames); (handle != rbtEnd(hostnames)); handle = rbtNext(hostnames, handle)) {
				rec = (treerec_t *)gettreeitem(hostnames, handle);

				if (wanted_host(rec->name)) {
					fprintf(output, "<OPTION VALUE=\"%s\">%s</OPTION>\n", rec->name, rec->name);
				}
			}
		}
		else if (strcmp(t_start, "JSHOSTLIST") == 0) {
			RbtIterator handle;

			fetch_board();
			clearflags(testnames);

			fprintf(output, "var hosts = new Array();\n");
			fprintf(output, "hosts[\"ALL\"] = [ \"ALL\"");
			for (handle = rbtBegin(testnames); (handle != rbtEnd(testnames)); handle = rbtNext(testnames, handle)) {
				treerec_t *rec = gettreeitem(testnames, handle);
				fprintf(output, ", \"%s\"", rec->name);
			}
			fprintf(output, " ];\n");

			for (handle = rbtBegin(hostnames); (handle != rbtEnd(hostnames)); handle = rbtNext(hostnames, handle)) {
				treerec_t *hrec = gettreeitem(hostnames, handle);
				if (wanted_host(hrec->name)) {
					RbtIterator thandle;
					treerec_t *trec;
					char *bwalk, *tname, *p;
					char *key = (char *)malloc(strlen(hrec->name) + 3);

					/* Setup the search key and find the first occurrence. */
					sprintf(key, "\n%s|", hrec->name);
					if (strncmp(statusboard, (key+1), strlen(key+1)) == 0)
						bwalk = statusboard;
					else {
						bwalk = strstr(statusboard, key);
						if (bwalk) bwalk++;
					}

					while (bwalk) {
						tname = bwalk + strlen(key+1);
						p = strchr(tname, '|'); if (p) *p = '\0';
						if ( (strcmp(tname, xgetenv("INFOCOLUMN")) != 0) &&
						     (strcmp(tname, xgetenv("TRENDSCOLUMN")) != 0) ) {
							thandle = rbtFind(testnames, tname);
							if (thandle != rbtEnd(testnames)) {
								trec = (treerec_t *)gettreeitem(testnames, thandle);
								trec->flag = 1;
							}
						}
						if (p) *p = '|';

						bwalk = strstr(tname, key); if (bwalk) bwalk++;
					}

					fprintf(output, "hosts[\"%s\"] = [ \"ALL\"", hrec->name);
					for (thandle = rbtBegin(testnames); (thandle != rbtEnd(testnames)); thandle = rbtNext(testnames, thandle)) {
						trec = (treerec_t *)gettreeitem(testnames, thandle);
						if (trec->flag == 0) continue;

						trec->flag = 0;
						fprintf(output, ", \"%s\"", trec->name);
					}
					fprintf(output, " ];\n");
				}
			}
		}
		else if (strcmp(t_start, "TESTLIST") == 0) {
			RbtIterator handle;
			treerec_t *rec;

			fetch_board();

			for (handle = rbtBegin(testnames); (handle != rbtEnd(testnames)); handle = rbtNext(testnames, handle)) {
				rec = (treerec_t *)gettreeitem(testnames, handle);
				fprintf(output, "<OPTION VALUE=\"%s\">%s</OPTION>\n", rec->name, rec->name);
			}
		}
		else if (strcmp(t_start, "DISABLELIST") == 0) {
			char *walk, *eoln;
			dishost_t *dhosts = NULL, *hwalk, *hprev;
			distest_t *twalk;

			fetch_board();
			clearflags(testnames);

			walk = statusboard;
			while (walk) {
				eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
				if (*walk) {
					char *buf, *hname, *tname, *dismsg, *p;
					time_t distime;
					RbtIterator thandle;
					treerec_t *rec;

					buf = strdup(walk);
					hname = tname = dismsg = NULL; distime = 0;

					hname = gettok(buf, "|");
					if (hname) tname = gettok(NULL, "|");
					if (tname) { p = gettok(NULL, "|"); if (p) distime = atol(p); }
					if (distime) dismsg = gettok(NULL, "|\n");

					if (hname && tname && (distime != 0) && dismsg && wanted_host(hname)) {
						nldecode(dismsg);
						hwalk = dhosts; hprev = NULL;
						while (hwalk && (strcasecmp(hname, hwalk->name) > 0)) {
							hprev = hwalk;
							hwalk = hwalk->next;
						}
						if (!hwalk || (strcasecmp(hname, hwalk->name) != 0)) {
							dishost_t *newitem = (dishost_t *) malloc(sizeof(dishost_t));
							newitem->name = strdup(hname);
							newitem->tests = NULL;
							newitem->next = hwalk;
							if (!hprev)
								dhosts = newitem;
							else 
								hprev->next = newitem;
							hwalk = newitem;
						}
						twalk = (distest_t *) malloc(sizeof(distest_t));
						twalk->name = strdup(tname);
						twalk->cause = strdup(dismsg);
						twalk->until = distime;
						twalk->next = hwalk->tests;
						hwalk->tests = twalk;

						thandle = rbtFind(testnames, tname);
						if (thandle != rbtEnd(testnames)) {
							rec = gettreeitem(testnames, thandle);
							rec->flag = 1;
						}
					}

					xfree(buf);
				}

				if (eoln) {
					*eoln = '\n';
					walk = eoln+1;
				}
				else {
					walk = NULL;
				}
			}

			if (dhosts) {
				/* Insert the "All hosts" record first. */
				hwalk = (dishost_t *)calloc(1, sizeof(dishost_t));
				hwalk->next = dhosts;
				dhosts = hwalk;

				for (hwalk = dhosts; (hwalk); hwalk = hwalk->next) {
					fprintf(output, "<TR>");
					fprintf(output, "<TD>");
					fprintf(output,"<form method=\"post\" action=\"%s/hobbit-enadis.sh\">\n",
						xgetenv("SECURECGIBINURL"));

					fprintf(output, "<table summary=\"%s disabled tests\" width=\"100%%\">\n", 
						(hwalk->name ? hwalk->name : ""));

					fprintf(output, "<tr>\n");
					fprintf(output, "<TH COLSPAN=3><I>%s</I></TH>", 
							(hwalk->name ? hwalk->name : "All hosts"));
					fprintf(output, "</tr>\n");


					fprintf(output, "<tr>\n");

					fprintf(output, "<td>\n");
					if (hwalk->name) {
						fprintf(output, "<input name=\"hostname\" type=hidden value=\"%s\">\n", 
							hwalk->name);

						fprintf(output, "<textarea name=\"%s causes\" rows=\"8\" cols=\"50\" readonly style=\"font-size: 10pt\">\n", hwalk->name);
						for (twalk = hwalk->tests; (twalk); twalk = twalk->next) {
							char *msg = twalk->cause;
							msg += strspn(msg, "0123456789 ");
							fprintf(output, "%s\n%s\nUntil: %s\n---------------------\n", 
								twalk->name, msg, 
								(twalk->until == -1) ? "OK" : ctime(&twalk->until));
						}
						fprintf(output, "</textarea>\n");
					}
					else {
						dishost_t *hw2;
						fprintf(output, "<select multiple size=8 name=\"hostname\">\n");
						for (hw2 = hwalk->next; (hw2); hw2 = hw2->next)
							fprintf(output, "<option value=\"%s\">%s</option>\n", 
								hw2->name, hw2->name);
						fprintf(output, "</select>\n");
					}
					fprintf(output, "</td>\n");

					fprintf(output, "<td align=center>\n");
					fprintf(output, "<select multiple size=8 name=\"enabletest\">\n");
					fprintf(output, "<option value=\"*\" selected>ALL</option>\n");
					if (hwalk->tests) {
						for (twalk = hwalk->tests; (twalk); twalk = twalk->next) {
							fprintf(output, "<option value=\"%s\">%s</option>\n",
								twalk->name, twalk->name);
						}
					}
					else {
						RbtIterator tidx;
						treerec_t *rec;

						for (tidx = rbtBegin(testnames); (tidx != rbtEnd(testnames)); tidx = rbtNext(testnames, tidx)) {
							rec = gettreeitem(testnames, tidx);
							if (rec->flag == 0) continue;

							fprintf(output, "<option value=\"%s\">%s</option>\n",
								rec->name, rec->name);
						}
					}
					fprintf(output, "</select>\n");
					fprintf(output, "</td>\n");

					fprintf(output, "<td align=center>\n");
					fprintf(output, "<input name=\"go\" type=submit value=\"Enable\">\n");
					fprintf(output, "</td>\n");

					fprintf(output, "</tr>\n");

					fprintf(output, "</table>\n");
					fprintf(output, "</form>\n");
					fprintf(output, "</td>\n");
					fprintf(output, "</TR>\n");
				}
			}
			else {
				fprintf(output, "<tr><th align=center colspan=3><i>No tests disabled</i></th></tr>\n");
			}
		}
		else if (strcmp(t_start, "SCHEDULELIST") == 0) {
			char *walk, *eoln;
			int gotany = 0;

			fetch_board();

			walk = scheduleboard;
			while (walk) {
				eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
				if (*walk) {
					int id = 0;
					time_t executiontime = 0;
					char *sender = NULL, *cmd = NULL, *buf, *p, *eoln;

					buf = strdup(walk);
					p = gettok(buf, "|");
					if (p) { id = atoi(p); p = gettok(NULL, "|"); }
					if (p) { executiontime = atoi(p); p = gettok(NULL, "|"); }
					if (p) { sender = p; p = gettok(NULL, "|"); }
					if (p) { cmd = p; }

					if (id && executiontime && sender && cmd) {
						gotany = 1;
						nldecode(cmd);
						fprintf(output, "<TR>\n");

						fprintf(output, "<TD>%s</TD>\n", ctime(&executiontime));

						fprintf(output, "<TD>");
						p = cmd;
						while ((eoln = strchr(p, '\n')) != NULL) {
							*eoln = '\0';
							fprintf(output, "%s<BR>", p);
							p = (eoln + 1);
						}
						fprintf(output, "</TD>\n");

						fprintf(output, "<td>\n");
						fprintf(output, "<form method=\"post\" action=\"%s/hobbit-enadis.sh\">\n",
							xgetenv("SECURECGIBINURL"));
						fprintf(output, "<input name=canceljob type=hidden value=\"%d\">\n", 
							id);
						fprintf(output, "<input name=go type=submit value=\"Cancel\">\n");
						fprintf(output, "</form></td>\n");

						fprintf(output, "</TR>\n");
					}
					xfree(buf);
				}

				if (eoln) {
					*eoln = '\n';
					walk = eoln+1;
				}
				else {
					walk = NULL;
				}
			}

			if (!gotany) {
				fprintf(output, "<tr><th align=center colspan=3><i>No tasks scheduled</i></th></tr>\n");
			}
		}

		else if (strcmp(t_start, "NKACKTTPRIO") == 0) fprintf(output, "%d", nkackttprio);
		else if (strcmp(t_start, "NKACKTTGROUP") == 0) fprintf(output, "%s", nkackttgroup);
		else if (strcmp(t_start, "NKACKTTEXTRA") == 0) fprintf(output, "%s", nkackttextra);
		else if (strcmp(t_start, "NKACKINFOURL") == 0) fprintf(output, "%s", nkackinfourl);
		else if (strcmp(t_start, "NKACKDOCURL") == 0) fprintf(output, "%s", nkackdocurl);

		else if (strcmp(t_start, "NKEDITUPDINFO") == 0) {
			fprintf(output, "%s", nkeditupdinfo);
		}

		else if (strcmp(t_start, "NKEDITPRIOLIST") == 0) {
			int i;
			char *selstr;

			for (i=1; (i <= 3); i++) {
				selstr = ((i == nkeditprio) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%d\" %s>%d</option>\n", i, selstr, i);
			}
		}

		else if (strcmp(t_start, "NKEDITCLONELIST") == 0) {
			int i;
			for (i=0; (nkeditclonelist[i]); i++) 
				fprintf(output, "<option value=\"%s\">%s</option>\n", 
					nkeditclonelist[i], nkeditclonelist[i]);
		}

		else if (strcmp(t_start, "NKEDITGROUP") == 0) {
			fprintf(output, "%s", nkeditgroup);
		}

		else if (strcmp(t_start, "NKEDITEXTRA") == 0) {
			fprintf(output, "%s", nkeditextra);
		}

		else if (strcmp(t_start, "NKEDITWKDAYS") == 0) {
			fprintf(output, wkdayselect('*', "All days", 1));
			fprintf(output, wkdayselect('W', "Mon-Fri", 0));
			fprintf(output, wkdayselect('1', "Monday", 0));
			fprintf(output, wkdayselect('2', "Tuesday", 0));
			fprintf(output, wkdayselect('3', "Wednesday", 0));
			fprintf(output, wkdayselect('4', "Thursday", 0));
			fprintf(output, wkdayselect('5', "Friday", 0));
			fprintf(output, wkdayselect('6', "Saturday", 0));
			fprintf(output, wkdayselect('0', "Sunday", 0));
		}

		else if (strcmp(t_start, "NKEDITSTART") == 0) {
			int i, curr;
			char *selstr;

			curr = (nkeditslastart ? (atoi(nkeditslastart) / 100) : 0);
			for (i=0; (i <= 23); i++) {
				selstr = ((i == curr) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%02i00\" %s>%02i:00</option>\n", i, selstr, i);
			}
		}

		else if (strcmp(t_start, "NKEDITEND") == 0) {
			int i, curr;
			char *selstr;

			curr = (nkeditslaend ? (atoi(nkeditslaend) / 100) : 24);
			for (i=1; (i <= 24); i++) {
				selstr = ((i == curr) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%02i00\" %s>%02i:00</option>\n", i, selstr, i);
			}
		}

		else if (strncmp(t_start, "NKEDITDAYLIST", 13) == 0) {
			time_t t = ((*(t_start+13) == '1') ? nkeditstarttime : nkeditendtime);
			char *defstr = ((*(t_start+13) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm *tm;

			tm = localtime(&t);

			selstr = ((t == 0) ? "SELECTED" : "");
			fprintf(output, "<option value=\"0\" %s>%s</option>\n", selstr, defstr);

			for (i=1; (i <= 31); i++) {
				selstr = ( (t && (tm->tm_mday == i)) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%d\" %s>%d</option>\n", i, selstr, i);
			}
		}

		else if (strncmp(t_start, "NKEDITMONLIST", 13) == 0) {
			time_t t = ((*(t_start+13) == '1') ? nkeditstarttime : nkeditendtime);
			char *defstr = ((*(t_start+13) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm tm;
			time_t now;
			struct tm nowtm;
			struct tm monthtm;
			char mname[20];

			memcpy(&tm, localtime(&t), sizeof(tm));

			now = getcurrenttime(NULL);
			memcpy(&nowtm, localtime(&now), sizeof(tm));

			selstr = ((t == 0) ? "SELECTED" : "");
			fprintf(output, "<option value=\"0\" %s>%s</option>\n", selstr, defstr);

			for (i=1; (i <= 12); i++) {
				selstr = ( (t && (tm.tm_mon == (i -1))) ? "SELECTED" : "");
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm.tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%s</option>\n", i, selstr, mname);
			}
		}

		else if (strncmp(t_start, "NKEDITYEARLIST", 14) == 0) {
			time_t t = ((*(t_start+14) == '1') ? nkeditstarttime : nkeditendtime);
			char *defstr = ((*(t_start+14) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm tm;
			time_t now;
			struct tm nowtm;
			int beginyear, endyear;

			memcpy(&tm, localtime(&t), sizeof(tm));

			now = getcurrenttime(NULL);
			memcpy(&nowtm, localtime(&now), sizeof(tm));

			beginyear = nowtm.tm_year + 1900;
			endyear = nowtm.tm_year + 1900 + 5;

			selstr = ((t == 0) ? "SELECTED" : "");
			fprintf(output, "<option value=\"0\" %s>%s</option>\n", selstr, defstr);

			for (i=beginyear; (i <= endyear); i++) {
				selstr = ( (t && (tm.tm_year == (i - 1900))) ? "SELECTED" : "");
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d</option>\n", i, selstr, i);
			}
		}

		else if (hostenv_hikey && (strncmp(t_start, "BBH_", 4) == 0)) {
			namelist_t *hinfo = hostinfo(hostenv_hikey);
			if (hinfo) {
				char *s = bbh_item_byname(hinfo, t_start);

				if (!s) {
					fprintf(output, "&%s", t_start);
				}
				else {
					fprintf(output, "%s", s);
				}
			}
		}

		else if (*t_start && (savechar == ';')) {
			/* A "&xxx;" is probably an HTML escape - output unchanged. */
			fprintf(output, "&%s", t_start);
		}

		else if (*t_start && (strncmp(t_start, "SELECT_", 7) == 0)) {
			/*
			 * Special for getting the SELECTED tag into list boxes.
			 * Cannot use xgetenv because it complains for undefined
			 * environment variables.
			 */
			char *val = getenv(t_start);

			fprintf(output, "%s", (val ? val : ""));
		}

		else if (strlen(t_start) && xgetenv(t_start)) {
			fprintf(output, "%s", xgetenv(t_start));
		}

		else fprintf(output, "&%s", t_start);		/* No substitution - copy all unchanged. */
			
		*t_next = savechar; t_start = t_next; t_next = strchr(t_start, '&');
	}

	/* Remainder of file */
	fprintf(output, "%s", t_start);
}


void headfoot(FILE *output, char *template, char *pagepath, char *head_or_foot, int bgcolor)
{
	int	fd;
	char 	filename[PATH_MAX];
	char    *bulletinfile;
	struct  stat st;
	char	*templatedata;
	char	*hfpath;

	MEMDEFINE(filename);

	if (xgetenv("HOBBITDREL") == NULL) {
		char *hobbitdrel = (char *)malloc(12+strlen(VERSION));
		sprintf(hobbitdrel, "HOBBITDREL=%s", VERSION);
		putenv(hobbitdrel);
	}

	/*
	 * "pagepath" is the relative path for this page, e.g. 
	 * - for "bb.html" it is ""
	 * - for a page, it is "pagename/"
	 * - for a subpage, it is "pagename/subpagename/"
	 *
	 * BB allows header/footer files named bb_PAGE_header or bb_PAGE_SUBPAGE_header
	 * so we need to scan for an existing file - starting with the
	 * most detailed one, and working up towards the standard "web/bb_TYPE" file.
	 */

	hfpath = strdup(pagepath); 
	/* Trim off excess trailing slashes */
	if (*hfpath) {
		while (*(hfpath + strlen(hfpath) - 1) == '/') *(hfpath + strlen(hfpath) - 1) = '\0';
	}
	fd = -1;

	hostenv_pagepath = strdup(hfpath);

	while ((fd == -1) && strlen(hfpath)) {
		char *p;
		char *elemstart;

		if (hostenv_templatedir) {
			sprintf(filename, "%s/", hostenv_templatedir);
		}
		else {
			sprintf(filename, "%s/web/", xgetenv("BBHOME"));
		}

		p = strchr(hfpath, '/'); elemstart = hfpath;
		while (p) {
			*p = '\0';
			strcat(filename, elemstart);
			strcat(filename, "_");
			*p = '/';
			p++;
			elemstart = p; p = strchr(elemstart, '/');
		}
		strcat(filename, elemstart);
		strcat(filename, "_");
		strcat(filename, head_or_foot);

		dbgprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);

		if (fd == -1) {
			p = strrchr(hfpath, '/');
			if (p == NULL) p = hfpath;
			*p = '\0';
		}
	}
	xfree(hfpath);

	if (fd == -1) {
		/* Fall back to default head/foot file. */
		if (hostenv_templatedir) {
			sprintf(filename, "%s/%s_%s", hostenv_templatedir, template, head_or_foot);
		}
		else {
			sprintf(filename, "%s/web/%s_%s", xgetenv("BBHOME"), template, head_or_foot);
		}

		dbgprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);
	}

	if (fd != -1) {
		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1);
		read(fd, templatedata, st.st_size);
		templatedata[st.st_size] = '\0';
		close(fd);

		output_parsed(output, templatedata, bgcolor, time(NULL));

		xfree(templatedata);
	}
	else {
		fprintf(output, "<HTML><BODY> \n <HR size=4> \n <BR>%s is either missing or invalid, please create this file with your custom header<BR> \n<HR size=4>", filename);
	}

	/* Check for bulletin files */
	bulletinfile = (char *)malloc(strlen(xgetenv("BBHOME")) + strlen("/web/bulletin_") + strlen(head_or_foot)+1);
	sprintf(bulletinfile, "%s/web/bulletin_%s", xgetenv("BBHOME"), head_or_foot);
	fd = open(bulletinfile, O_RDONLY);
	if (fd != -1) {
		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1);
		read(fd, templatedata, st.st_size);
		templatedata[st.st_size] = '\0';
		close(fd);
		output_parsed(output, templatedata, bgcolor, time(NULL));
		xfree(templatedata);
	}

	xfree(hostenv_pagepath); hostenv_pagepath = NULL;
	xfree(bulletinfile);

	MEMUNDEFINE(filename);
}

void showform(FILE *output, char *headertemplate, char *formtemplate, int color, time_t seltime, 
	      char *pretext, char *posttext)
{
	/* Present the query form */
	int formfile;
	char formfn[PATH_MAX];

	sprintf(formfn, "%s/web/%s", xgetenv("BBHOME"), formtemplate);
	formfile = open(formfn, O_RDONLY);

	if (formfile >= 0) {
		char *inbuf;
		struct stat st;

		fstat(formfile, &st);
		inbuf = (char *) malloc(st.st_size + 1);
		read(formfile, inbuf, st.st_size);
		inbuf[st.st_size] = '\0';
		close(formfile);

		headfoot(output, headertemplate, "", "header", color);
		if (pretext) fprintf(output, "%s", pretext);
		output_parsed(output, inbuf, color, seltime);
		if (posttext) fprintf(output, "%s", posttext);
		headfoot(output, headertemplate, "", "footer", color);

		xfree(inbuf);
	}
}

