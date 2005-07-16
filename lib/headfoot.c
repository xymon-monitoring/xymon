/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for handling header- and footer-files.                */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: headfoot.c,v 1.34 2005-07-16 21:14:40 henrik Exp $";

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
static char **hostlist = NULL;
static int hostcount = 0;
static char **testlist = NULL;
static int testcount = 0;

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

static int namecompare(const void *v1, const void *v2)
{
	char **n1 = (char **)v1;
	char **n2 = (char **)v2;
	int result;

	result = strcmp(*n1, *n2);

	return result;
}

static void fetch_board(void)
{
	char *walk, *eoln;
	int i;

	if (sendmessage("hobbitdboard fields=hostname,testname,disabletime,dismsg", 
			NULL, NULL, &statusboard, 1, BBTALK_TIMEOUT) != BB_OK)
		return;

	walk = statusboard;
	while (walk) {
		eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
		if (strlen(walk) && (strncmp(walk, "summary|", 8) != 0) && (strncmp(walk, "dialup|", 7) != 0)) {
			char *buf, *hname = NULL, *tname = NULL;

			buf = strdup(walk);

			hname = gettok(buf, "|");
			if (hname) tname = gettok(NULL, "|");

			if (hostcount == 0) {
				hostcount++;
				hostlist = (char **)malloc(sizeof(char *));
				hostlist[0] = strdup(hname);
			}
			else {
				for (i = 0; ((i < hostcount) && strcmp(hname, hostlist[i])); i++) ;
				if (i == hostcount) {
					hostcount++;
					hostlist = (char **)realloc(hostlist, hostcount * sizeof(char *));
					hostlist[hostcount-1] = strdup(hname);
				}
			}

			if (testcount == 0) {
				testcount++;
				testlist = (char **)malloc(sizeof(char *));
				testlist[0] = strdup(tname);
			}
			else {
				for (i = 0; ((i < testcount) && strcmp(tname, testlist[i])); i++) ;
				if (i == testcount) {
					testcount++;
					testlist = (char **)realloc(testlist, testcount * sizeof(char *));
					testlist[testcount-1] = strdup(tname);
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

	if (hostcount) qsort(hostlist, hostcount, sizeof(char *), namecompare);
	if (testcount) qsort(testlist, testcount, sizeof(char *), namecompare);

	if (sendmessage("schedule", NULL, NULL, &scheduleboard, 1, BBTALK_TIMEOUT) != BB_OK)
		return;
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

void output_parsed(FILE *output, char *templatedata, int bgcolor, char *pagetype, time_t selectedtime)
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
		else if (strcmp(t_start, "REPHOURLIST") == 0) { 
			int i; 
			struct tm *nowtm = localtime(&yesterday); 
			char *selstr;

			for (i=0; (i <= 24); i++) {
				if (i == nowtm->tm_hour) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
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
			int i;

			if (!hostlist) fetch_board();

			for (i = 0; (i < hostcount); i++) {
				if (wanted_host(hostlist[i])) {
					fprintf(output, "<OPTION VALUE=\"%s\">%s</OPTION>\n", hostlist[i], hostlist[i]);
				}
			}
		}
		else if (strcmp(t_start, "JSHOSTLIST") == 0) {
			int i, tcount, tidx;
			char **tlist;

			if (!hostlist) fetch_board();

			tlist = malloc((testcount+1) * sizeof(char *));

			fprintf(output, "var hosts = new Array();\n");
			fprintf(output, "hosts[\"ALL\"] = [ \"ALL\"");
			for (tidx = 0; (tidx < testcount); tidx++) {
				fprintf(output, ", \"%s\"", testlist[tidx]);
			}
			fprintf(output, " ];\n");

			for (i = 0; (i < hostcount); i++) {
				if (wanted_host(hostlist[i])) {
					char *bwalk, *tname, *p;
					char *key = (char *)malloc(strlen(hostlist[i]) + 3);

					tcount = 0;

					/* Setup the search key and find the first occurrence. */
					sprintf(key, "\n%s|", hostlist[i]);
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
							tlist[tcount++] = strdup(tname);
						}
						if (p) *p = '|';

						bwalk = strstr(tname, key); if (bwalk) bwalk++;
					}
					if (tcount) qsort(tlist, tcount, sizeof(char *), namecompare);

					fprintf(output, "hosts[\"%s\"] = [ \"ALL\"", hostlist[i]);
					for (tidx = 0; (tidx < tcount); tidx++) {
						fprintf(output, ", \"%s\"", tlist[tidx]);
						xfree(tlist[tidx]);
					}
					fprintf(output, " ];\n");

					xfree(key);
				}
			}

			xfree(tlist);
		}
		else if (strcmp(t_start, "TESTLIST") == 0) {
			int i;

			if (!testlist) fetch_board();

			for (i = 0; (i < testcount); i++) {
				fprintf(output, "<OPTION VALUE=\"%s\">%s</OPTION>\n", testlist[i], testlist[i]);
			}
		}
		else if (strcmp(t_start, "DISABLELIST") == 0) {
			char *walk, *eoln;
			dishost_t *dhosts = NULL, *hwalk, *hprev;
			distest_t *twalk;
			char **alltests = NULL;
			int alltestcount = 0, i;

			if (!statusboard || !hostlist) fetch_board();

			walk = statusboard;
			while (walk) {
				eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
				if (*walk) {
					char *buf, *hname, *tname, *dismsg, *p;
					time_t distime;

					buf = strdup(walk);
					hname = tname = dismsg = NULL; distime = 0;

					hname = gettok(buf, "|");
					if (hname) tname = gettok(NULL, "|");
					if (tname) { p = gettok(NULL, "|"); if (p) distime = atoi(p); }
					if (distime) dismsg = gettok(NULL, "|\n");

					if (hname && tname && (distime > 0) && dismsg && wanted_host(hname)) {
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

						for (i=0; ((i < alltestcount) && strcmp(alltests[i], tname)); i++) ;
						if (i == alltestcount) {
							if (alltests == NULL) {
								alltests = (char **)malloc(sizeof(char *));
							}
							else {
								alltests = (char **)realloc(alltests, (alltestcount+1)*sizeof(char *));
							}
							alltests[alltestcount++] = strdup(tname);
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
				if (alltestcount) qsort(alltests, alltestcount, sizeof(char *), namecompare);

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
								twalk->name, msg, ctime(&twalk->until));
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
						int i;
						for (i = 0; (i < alltestcount); i++) {
							fprintf(output, "<option value=\"%s\">%s</option>\n",
								alltests[i], alltests[i]);
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

			if (!scheduleboard) fetch_board();

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

		else if (strlen(t_start) && xgetenv(t_start)) {
			fprintf(output, "%s", xgetenv(t_start));
		}

		else fprintf(output, "&%s", t_start);		/* No substitution - copy all unchanged. */
			
		*t_next = savechar; t_start = t_next; t_next = strchr(t_start, '&');
	}

	/* Remainder of file */
	fprintf(output, "%s", t_start);
}


void headfoot(FILE *output, char *pagetype, char *pagepath, char *head_or_foot, int bgcolor)
{
	int	fd;
	char 	filename[PATH_MAX];
	struct stat st;
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

		dprintf("Trying header/footer file '%s'\n", filename);
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
			sprintf(filename, "%s/%s_%s", hostenv_templatedir, pagetype, head_or_foot);
		}
		else {
			sprintf(filename, "%s/web/%s_%s", xgetenv("BBHOME"), pagetype, head_or_foot);
		}

		dprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);
	}

	if (fd != -1) {
		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1);
		read(fd, templatedata, st.st_size);
		templatedata[st.st_size] = '\0';
		close(fd);

		output_parsed(output, templatedata, bgcolor, pagetype, time(NULL));

		xfree(templatedata);
	}
	else {
		fprintf(output, "<HTML><BODY> \n <HR size=4> \n <BR>%s is either missing or invalid, please create this file with your custom header<BR> \n<HR size=4>", filename);
	}

	xfree(hostenv_pagepath); hostenv_pagepath = NULL;
	MEMUNDEFINE(filename);
}

