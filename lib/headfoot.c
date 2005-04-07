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

static char rcsid[] = "$Id: headfoot.c,v 1.21 2005-04-07 10:09:02 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include "libbbgen.h"
#include "version.h"

int	unpatched_bbd = 0;

/* Stuff for headfoot - variables we can set dynamically */
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

void sethostenv(char *host, char *ip, char *svc, char *color)
{
	if (hostenv_host)  xfree(hostenv_host);
	if (hostenv_ip)    xfree(hostenv_ip);
	if (hostenv_svc)   xfree(hostenv_svc);
	if (hostenv_color) xfree(hostenv_color);

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

void output_parsed(FILE *output, char *templatedata, int bgcolor, char *pagetype, time_t selectedtime)
{
	char	*t_start, *t_next;
	char	savechar;
	time_t	now = time(NULL);
	time_t  yesterday = time(NULL) - 86400;
	struct tm *nowtm;

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
			if (unpatched_bbd && (strcmp(pagetype, "hostsvc") == 0)) {
				fprintf(output, "%s/bkg-%s.gif", 
					xgetenv("BBSKIN"), colorname(bgcolor));
			}
			else {
				fprintf(output, "%s", colorname(bgcolor));
			}
		}
		else if (strcmp(t_start, "BBCOLOR") == 0)       fprintf(output, "%s", hostenv_color);
		else if (strcmp(t_start, "BBSVC") == 0)         fprintf(output, "%s", hostenv_svc);
		else if (strcmp(t_start, "BBHOST") == 0)        fprintf(output, "%s", hostenv_host);
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
	while (*(hfpath + strlen(hfpath) - 1) == '/') {
		*(hfpath + strlen(hfpath) - 1) = '\0';
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

