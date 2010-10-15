/*----------------------------------------------------------------------------*/
/* Hobbit report generation front-end.                                        */
/*                                                                            */
/* This is a front-end CGI that lets the user select reporting parameters,    */
/* and then invokes bbgen to generate the report. When the report is ready,   */
/* the user's browser is sent off to view the report.                         */
/*                                                                            */
/* Copyright (C) 2003-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#include "libbbgen.h"

char *reqenv[] = {
"BBHOME",
"BBREP",
"BBREPURL",
NULL };

char *style = "";
time_t starttime = 0;
time_t endtime = 0;
char *suburl = "";
int  csvoutput = 0;
char csvdelim = ',';
cgidata_t *cgidata = NULL;

char *monthnames[13] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };

void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	cgidata_t *cwalk;
	int startday, startmon, startyear;
	int endday, endmon, endyear;
	struct tm tmbuf;

	startday = startmon = startyear = endday = endmon = endyear = -1;
	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "start-day") == 0) {
			startday = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "start-mon") == 0) {
			char *errptr;

			startmon = strtol(cwalk->value, &errptr, 10) - 1;
			if (errptr == cwalk->value) {
				for (startmon=0; (monthnames[startmon] && strcmp(cwalk->value, monthnames[startmon])); startmon++) ;
				if (startmon >= 12) startmon = -1;
			}
		}
		else if (strcasecmp(cwalk->name, "start-yr") == 0) {
			startyear = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "end-day") == 0) {
			endday = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "end-mon") == 0) {
			char *errptr;

			endmon = strtol(cwalk->value, &errptr, 10) - 1;
			if (errptr == cwalk->value) {
				for (endmon=0; (monthnames[endmon] && strcmp(cwalk->value, monthnames[endmon])); endmon++) ;
				if (endmon > 12) endmon = -1;
			}
		}
		else if (strcasecmp(cwalk->name, "end-yr") == 0) {
			endyear = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "style") == 0) {
			style = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "suburl") == 0) {
			suburl = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "DoReport") == 0) {
			csvoutput = 0;
		}
		else if (strcasecmp(cwalk->name, "DoCSV") == 0) {
			csvoutput = 1;
		}
		else if (strcasecmp(cwalk->name, "csvdelim") == 0) {
			csvdelim = *cwalk->value;
		}

		cwalk = cwalk->next;
	}

	memset(&tmbuf, 0, sizeof(tmbuf));
	tmbuf.tm_mday = startday;
	tmbuf.tm_mon = startmon;
	tmbuf.tm_year = startyear - 1900;
	tmbuf.tm_hour = 0;
	tmbuf.tm_min = 0;
	tmbuf.tm_sec = 0;
	tmbuf.tm_isdst = -1;		/* Important! Or we mishandle DST periods */
	starttime = mktime(&tmbuf);

	memset(&tmbuf, 0, sizeof(tmbuf));
	tmbuf.tm_mday = endday;
	tmbuf.tm_mon = endmon;
	tmbuf.tm_year = endyear - 1900;
	tmbuf.tm_hour = 23;
	tmbuf.tm_min = 59;
	tmbuf.tm_sec = 59;
	tmbuf.tm_isdst = -1;		/* Important! Or we mishandle DST periods */
	endtime = mktime(&tmbuf);

	if ((starttime == -1) || (endtime == -1) || (starttime > getcurrenttime(NULL))) errormsg("Invalid parameters");

	if (endtime > getcurrenttime(NULL)) endtime = getcurrenttime(NULL);

	if (starttime > endtime) {
		/* Swap start and end times */
		time_t tmp;

		tmp = endtime;
		endtime = starttime;
		starttime = tmp;
	}
}


void cleandir(char *dirname)
{
	DIR *dir;
	struct dirent *d;
	struct stat st;
	char fn[PATH_MAX];
	time_t killtime = getcurrenttime(NULL)-86400;

	dir = opendir(dirname);
	if (dir == NULL) return;

	while ((d = readdir(dir))) {
		if (d->d_name[0] != '.') {
			sprintf(fn, "%s/%s", dirname, d->d_name);
			if ((stat(fn, &st) == 0) && (st.st_mtime < killtime)) {
				if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
					dbgprintf("rm %s\n", fn);
					unlink(fn);
				}
				else if (S_ISDIR(st.st_mode)) {
					dbgprintf("Cleaning directory %s\n", fn);
					cleandir(fn);
					dbgprintf("rmdir %s\n", fn);
					rmdir(fn);
				}
				else { /* Ignore file */ };
			}
		}
	}
}


int main(int argc, char *argv[])
{
	char dirid[PATH_MAX];
	char outdir[PATH_MAX];
	char bbwebenv[PATH_MAX];
	char bbgencmd[PATH_MAX];
	char bbgentimeopt[100];
	char csvdelimopt[100];
	char *bbgen_argv[20];
	pid_t childpid;
	int childstat;
	char htmldelim[20];
	char startstr[20], endstr[20];
	int cleanupoldreps = 1;
	int argi, newargi;
	char *envarea = NULL;
	char *useragent = NULL;
	int usemultipart = 1;

	newargi = 0;
	bbgen_argv[newargi++] = bbgencmd;
	bbgen_argv[newargi++] = bbgentimeopt;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[1], "--noclean") == 0) {
			cleanupoldreps = 0;
		}
		else {
			bbgen_argv[newargi++] = argv[argi];
		}
	}

	redirect_cgilog("bb-rep");

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, "report", "report_form", COL_BLUE, getcurrenttime(NULL)-86400, NULL, NULL);
		return 0;
	}

	useragent = getenv("HTTP_USER_AGENT");
	if (useragent && strstr(useragent, "KHTML")) {
		/* KHTML (Konqueror, Safari) cannot handle multipart documents. */
		usemultipart = 0;
	}

	envcheck(reqenv);
	parse_query();

	/*
	 * We need to set these variables up AFTER we have put them into the bbgen_argv[] array.
	 * We cannot do it before, because we need the environment that the command-line options 
	 * might provide.
	 */
	if (xgetenv("BBGEN")) sprintf(bbgencmd, "%s", xgetenv("BBGEN"));
	else sprintf(bbgencmd, "%s/bin/bbgen", xgetenv("BBHOME"));

	sprintf(bbgentimeopt, "--reportopts=%u:%u:1:%s", (unsigned int)starttime, (unsigned int)endtime, style);

	sprintf(dirid, "%u-%u", (unsigned int)getpid(), (unsigned int)getcurrenttime(NULL));
	if (!csvoutput) {
		sprintf(outdir, "%s/%s", xgetenv("BBREP"), dirid);
		mkdir(outdir, 0755);
		bbgen_argv[newargi++] = outdir;
		sprintf(bbwebenv, "BBWEB=%s/%s", xgetenv("BBREPURL"), dirid);
		putenv(bbwebenv);
	}
	else {
		sprintf(outdir, "--csv=%s/%s.csv", xgetenv("BBREP"), dirid);
		bbgen_argv[newargi++] = outdir;
		sprintf(csvdelimopt, "--csvdelim=%c", csvdelim);
		bbgen_argv[newargi++] = csvdelimopt;
	}

	bbgen_argv[newargi++] = NULL;

	if (usemultipart) {
		/* Output the "please wait for report ... " thing */
		sprintf(htmldelim, "bbrep-%u-%u", (int)getpid(), (unsigned int)getcurrenttime(NULL));
		printf("Content-type: multipart/mixed;boundary=%s\n", htmldelim);
		printf("\n");
		printf("--%s\n", htmldelim);
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

		/* It's ok with these hardcoded values, as they are not used for this page */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		sethostenv_report(starttime, endtime, 97.0, 99.995);
		headfoot(stdout, "bbrep", "", "header", COL_BLUE);

		strftime(startstr, sizeof(startstr), "%b %d %Y", localtime(&starttime));
		strftime(endstr, sizeof(endstr), "%b %d %Y", localtime(&endtime));
		printf("<CENTER><A NAME=begindata>&nbsp;</A>\n");
		printf("<BR><BR><BR><BR>\n");
		printf("<H3>Generating report for the period: %s - %s (%s)<BR>\n", startstr, endstr, style);
		printf("<P><P>\n");
		fflush(stdout);
	}

	/* Go do the report */
	childpid = fork();
	if (childpid == 0) {
		execv(bbgencmd, bbgen_argv);
	}
	else if (childpid > 0) {
		wait(&childstat);

		/* Ignore SIGHUP so we dont get killed during cleanup of BBREP */
		signal(SIGHUP, SIG_IGN);

		if (WIFEXITED(childstat) && (WEXITSTATUS(childstat) != 0) ) {
			char msg[4096];

			if (usemultipart) printf("--%s\n\n", htmldelim);
			sprintf(msg, "Could not generate report.<br>\nCheck that the %s/www/rep/ directory has permissions '-rwxrwxr-x' (775)<br>\n and that is is set to group %d", xgetenv("BBHOME"), (int)getgid());
			errormsg(msg);
		}
		else {
			/* Send the browser off to the report */
			if (usemultipart) {
				printf("Done...Report is <A HREF=\"%s/%s/%s\">here</a>.</P></BODY></HTML>\n", xgetenv("BBREPURL"), dirid, suburl);
				fflush(stdout);
				printf("--%s\n\n", htmldelim);
			}
			printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			printf("<HTML><HEAD>\n");
			if (!csvoutput) {
				printf("<META HTTP-EQUIV=\"REFRESH\" CONTENT=\"0; URL=%s/%s/%s\"\n", 
					xgetenv("BBREPURL"), dirid, suburl);
				printf("</HEAD><BODY>Report is available <a href=\"%s/%s/%s\">here</a></BODY></HTML>\n",
					xgetenv("BBREPURL"), dirid, suburl);
			}
			else {
				printf("<META HTTP-EQUIV=\"REFRESH\" CONTENT=\"0; URL=%s/%s.csv\"\n", 
					xgetenv("BBREPURL"), dirid);
				printf("</HEAD><BODY>Report is available <a href=\"%s/%s.csv\">here</a></BODY></HTML>\n",
					xgetenv("BBREPURL"), dirid);
			}
			if (usemultipart) printf("\n--%s\n", htmldelim);
			fflush(stdout);
		}

		if (cleanupoldreps) cleandir(xgetenv("BBREP"));
	}
	else {
		if (usemultipart) printf("--%s\n\n", htmldelim);
		printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		errormsg("Fork failed");
	}

	return 0;
}

