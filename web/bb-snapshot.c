/*----------------------------------------------------------------------------*/
/* Hobbit "snapshot" CGI front-end.                                           */
/*                                                                            */
/* This is a CGI front-end that lets the user pick which snapshot view is     */
/* generated. A snapshot is a view of the Hobbit webpages from any time in    */
/* the past, generated from the history logs.                                 */
/*                                                                            */
/* Copyright (C) 2003-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-snapshot.c,v 1.25 2006-08-11 13:07:52 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>

#include "libbbgen.h"

time_t starttime = 0;
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
	int day, mon, year, hour, min, sec;
	struct tm tmbuf;
	cgidata_t *cwalk;

	day = mon = year = hour = min = sec = -1;
	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "day") == 0) {
			day = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "mon") == 0) {
			char *errptr;

			mon = strtol(cwalk->value, &errptr, 10) - 1;
			if (errptr == cwalk->value) {
				for (mon=0; (monthnames[mon] && strcmp(cwalk->value, monthnames[mon])); mon++) ;
				if (mon >= 12) mon = -1;
			}
		}
		else if (strcasecmp(cwalk->name, "yr") == 0) {
			year = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "hour") == 0) {
			hour = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "min") == 0) {
			min = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "sec") == 0) {
			sec = atoi(cwalk->value);
		}

		cwalk = cwalk->next;
	}

	memset(&tmbuf, 0, sizeof(tmbuf));
	tmbuf.tm_mday = day;
	tmbuf.tm_mon = mon;
	tmbuf.tm_year = year - 1900;
	tmbuf.tm_hour = hour;
	tmbuf.tm_min = min;
	tmbuf.tm_sec = sec;
	tmbuf.tm_isdst = -1;		/* Important! Or we mishandle DST periods */
	starttime = mktime(&tmbuf);

	if ((starttime == -1) || (starttime > time(NULL))) errormsg("Invalid parameters");
}


void cleandir(char *dirname)
{
	DIR *dir;
	struct dirent *d;
	struct stat st;
	char fn[PATH_MAX];
	time_t killtime = time(NULL)-86400;

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
	char bbgencmd[PATH_MAX];
	char bbwebenv[PATH_MAX];
	char bbgentimeopt[100];
	char *bbgen_argv[20];
	pid_t childpid;
	int childstat;
	char htmldelim[20];
	char startstr[20];
	int argi, newargi;
	char *envarea = NULL;
	char *useragent;
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
		else {
			bbgen_argv[newargi++] = argv[argi];
		}
	}
	bbgen_argv[newargi++] = outdir;
	bbgen_argv[newargi++] = NULL;

	redirect_cgilog("bb-snapshot");

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, "snapshot", "snapshot_form", COL_BLUE, getcurrenttime(NULL), NULL, NULL);
		return 0;
	}

	parse_query();

	useragent = getenv("HTTP_USER_AGENT");
	if (useragent && strstr(useragent, "KHTML")) {
		/* KHTML (Konqueror, Safari) cannot handle multipart documents. */
		usemultipart = 0;
	}

	/*
	 * Need to set these up AFTER putting them into bbgen_argv, since we
	 * need to have option parsing done first.
	 */
	if (xgetenv("BBGEN")) sprintf(bbgencmd, "%s", xgetenv("BBGEN"));
	else sprintf(bbgencmd, "%s/bin/bbgen", xgetenv("BBHOME"));

	sprintf(bbgentimeopt, "--snapshot=%u", (unsigned int)starttime);

	sprintf(dirid, "%u-%u", (unsigned int)getpid(), (unsigned int)time(NULL));
	sprintf(outdir, "%s/%s", xgetenv("BBSNAP"), dirid);
	if (mkdir(outdir, 0755) == -1) errormsg("Cannot create output directory");

	sprintf(bbwebenv, "BBWEB=%s/%s", xgetenv("BBSNAPURL"), dirid);
	putenv(bbwebenv);

	if (usemultipart) {
		/* Output the "please wait for report ... " thing */
		sprintf(htmldelim, "bbrep-%u-%u", (int)getpid(), (unsigned int)time(NULL));
		printf("Content-type: multipart/mixed;boundary=%s\n", htmldelim);
		printf("\n");
		printf("%s\n", htmldelim);
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

		/* It's ok with these hardcoded values, as they are not used for this page */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		sethostenv_report(starttime, starttime, 97.0, 99.995);
		headfoot(stdout, "snapshot", "", "header", COL_BLUE);

		strftime(startstr, sizeof(startstr), "%b %d %Y", localtime(&starttime));
		printf("<CENTER><A NAME=begindata>&nbsp;</A>\n");
		printf("<BR><BR><BR><BR>\n");
		printf("<H3>Generating snapshot: %s<BR>\n", startstr);
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

		/* Ignore SIGHUP so we dont get killed during cleanup of BBSNAP */
		signal(SIGHUP, SIG_IGN);

		if (WIFEXITED(childstat) && (WEXITSTATUS(childstat) != 0) ) {
			if (usemultipart) printf("%s\n\n", htmldelim);
			printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			errormsg("Could not generate report");
		}
		else {
			/* Send the browser off to the report */
			if (usemultipart) {
				printf("Done...<P></BODY></HTML>\n");
				fflush(stdout);
				printf("%s\n\n", htmldelim);
			}
			printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			printf("<HTML><HEAD>\n");
			printf("<META HTTP-EQUIV=\"REFRESH\" CONTENT=\"0; URL=%s/%s/\"\n", 
					xgetenv("BBSNAPURL"), dirid);
			printf("</HEAD><BODY BGCOLOR=\"000000\"></BODY></HTML>\n");
			if (usemultipart) printf("\n%s\n", htmldelim);
			fflush(stdout);
		}

		cleandir(xgetenv("BBSNAP"));
	}
	else {
		if (usemultipart) printf("%s\n\n", htmldelim);
		printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		errormsg("Fork failed");
	}

	return 0;
}

