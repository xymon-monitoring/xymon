/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for handling header- and footer-files.                */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: headfoot.c,v 1.3 2004-12-11 08:21:38 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include "color.h"
#include "errormsg.h"
#include "headfoot.h"
#include "version.h"

int	unpatched_bbd = 0;

/* Stuff for headfoot - variables we can set dynamically */
static char hostenv_svc[20];
static char hostenv_host[200];
static char hostenv_ip[20];
static char hostenv_color[20];
static time_t hostenv_reportstart = 0;
static time_t hostenv_reportend = 0;
static char hostenv_repwarn[20];
static char hostenv_reppanic[20];
static time_t hostenv_snapshot = 0;
static char *hostenv_logtime = NULL;
static char *hostenv_templatedir = NULL;

void sethostenv(char *host, char *ip, char *svc, char *color)
{
	hostenv_host[0] = hostenv_ip[0] = hostenv_svc[0] = hostenv_color[0] = '\0';
	strncat(hostenv_host,  host,  sizeof(hostenv_host)-1);
	strncat(hostenv_ip,    ip,    sizeof(hostenv_ip)-1);
	strncat(hostenv_svc,   svc,   sizeof(hostenv_svc)-1);
	strncat(hostenv_color, color, sizeof(hostenv_color)-1);
}

void sethostenv_report(time_t reportstart, time_t reportend, double repwarn, double reppanic)
{
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
	if (hostenv_logtime) free(hostenv_logtime);
	hostenv_logtime = strdup(histtime);
}

void sethostenv_template(char *dir)
{
	if (hostenv_templatedir) free(hostenv_templatedir);
	hostenv_templatedir = strdup(dir);
}

void output_parsed(FILE *output, char *templatedata, int bgcolor, char *pagetype)
{
	char	*t_start, *t_next;
	char	savechar;
	time_t	now = time(NULL);
	time_t  yesterday = time(NULL) - 86400;

	for (t_start = templatedata, t_next = strchr(t_start, '&'); (t_next); ) {
		/* Copy from t_start to t_next unchanged */
		*t_next = '\0'; t_next++;
		fprintf(output, "%s", t_start);

		/* Find token */
		for (t_start = t_next; ((*t_next >= 'A') && (*t_next <= 'Z')); t_next++ ) ;
		savechar = *t_next; *t_next = '\0';

		if (strcmp(t_start, "BBREL") == 0)     		fprintf(output, "%s", getenv("BBREL"));
		else if (strcmp(t_start, "BBRELDATE") == 0) 	fprintf(output, "%s", getenv("BBRELDATE"));
		else if (strcmp(t_start, "BBGENDREL") == 0) 	fprintf(output, "%s", getenv("BBGENDREL"));
		else if (strcmp(t_start, "BBSKIN") == 0)    	fprintf(output, "%s", getenv("BBSKIN"));
		else if (strcmp(t_start, "BBWEB") == 0)     	fprintf(output, "%s", getenv("BBWEB"));
		else if (strcmp(t_start, "CGIBINURL") == 0) 	fprintf(output, "%s", getenv("CGIBINURL"));

		else if (strcmp(t_start, "BBDATE") == 0) {
			char *bbdatefmt = getenv("BBDATEFORMAT");
			char datestr[100];

			/*
			 * If no BBDATEFORMAT setting, use a format string that
			 * produces output similar to that from ctime()
			 */
			if (bbdatefmt == NULL) bbdatefmt = "%a %b %d %H:%M:%S %Y\n";

			if (hostenv_reportstart != 0) {
				char starttime[20], endtime[20];

				strftime(starttime, sizeof(starttime), "%b %d %Y", localtime(&hostenv_reportstart));
				strftime(endtime, sizeof(endtime), "%b %d %Y", localtime(&hostenv_reportend));
				if (strcmp(starttime, endtime) == 0)
					fprintf(output, "%s", starttime);
				else
					fprintf(output, "%s - %s", starttime, endtime);
			}
			else if (hostenv_snapshot != 0) {
				strftime(datestr, sizeof(datestr), bbdatefmt, localtime(&hostenv_snapshot));
				fprintf(output, "%s", datestr);
			}
			else {
				strftime(datestr, sizeof(datestr), bbdatefmt, localtime(&now));
				fprintf(output, "%s", datestr);
			}
		}

		else if (strcmp(t_start, "BBBACKGROUND") == 0)  {
			if (unpatched_bbd && (strcmp(pagetype, "hostsvc") == 0)) {
				fprintf(output, "%s/bkg-%s.gif", 
					getenv("BBSKIN"), colorname(bgcolor));
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

		else if (strcmp(t_start, "REPMONLIST") == 0) {
			int i;
			struct tm *nowtm = localtime(&yesterday);
			struct tm monthtm;
			char mname[20];
			char *selstr;

			for (i=1; (i <= 12); i++) {
				if (i == (nowtm->tm_mon + 1)) selstr = "SELECTED"; else selstr = "";
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%s\n", i, selstr, mname);
			}
		}
		else if (strcmp(t_start, "REPDAYLIST") == 0) {
			int i;
			struct tm *nowtm = localtime(&yesterday);
			char *selstr;

			for (i=1; (i <= 31); i++) {
				if (i == nowtm->tm_mday) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "REPYEARLIST") == 0) {
			int i;
			struct tm *nowtm = localtime(&yesterday);
			char *selstr;

			for (i=1999; (i <= 2009); i++) {
				if (i == (nowtm->tm_year + 1900)) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}

		else fprintf(output, "&");			/* No substitution - copy the ampersand */
			
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

	if (getenv("BBGENDREL") == NULL) {
		char *bbgendrel = (char *)malloc(11+strlen(VERSION));
		sprintf(bbgendrel, "BBGENDREL=%s", VERSION);
		putenv(bbgendrel);
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

	while ((fd == -1) && strlen(hfpath)) {
		char *p;
		char *elemstart;

		if (hostenv_templatedir) {
			sprintf(filename, "%s/", hostenv_templatedir);
		}
		else {
			sprintf(filename, "%s/web/", getenv("BBHOME"));
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
	free(hfpath);

	if (fd == -1) {
		/* Fall back to default head/foot file. */
		if (hostenv_templatedir) {
			sprintf(filename, "%s/%s_%s", hostenv_templatedir, pagetype, head_or_foot);
		}
		else {
			sprintf(filename, "%s/web/%s_%s", getenv("BBHOME"), pagetype, head_or_foot);
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

		output_parsed(output, templatedata, bgcolor, pagetype);

		free(templatedata);
	}
	else {
		fprintf(output, "<HTML><BODY> \n <HR size=4> \n <BR>%s is either missing or invalid, please create this file with your custom header<BR> \n<HR size=4>", filename);
	}
}

