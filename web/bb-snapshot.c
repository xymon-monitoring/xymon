/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-snapshot.c,v 1.5 2004-11-18 15:40:08 henrik Exp $";

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

#include "bbgen.h"

/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 *	mon=Jun&
 *	day=19&
 *	yr=2003&
 *	hour=19&
 *	min=35&
 *	sec=21
 *
 */

char *reqenv[] = {
"BBHOME",
"BBSNAP",
"BBSNAPURL",
NULL };

time_t starttime = 0;

char *monthnames[13] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };

void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	char *query, *token;
	int day, mon, year, hour, min, sec;
	struct tm tmbuf;

	day = mon = year = hour = min = sec = -1;

	if (getenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request");
		return;
	}
	else query = urldecode("QUERY_STRING");

	if (!urlvalidate(query, NULL)) {
		errormsg("Invalid request");
		return;
	}

	token = strtok(query, "&");
	while (token) {
		char *val;
		
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }

		if (argnmatch(token, "day")) {
			day = atoi(val);
		}
		else if (argnmatch(token, "mon")) {
			for (mon=0; (monthnames[mon] && strcmp(val, monthnames[mon])); mon++) ;
			if (mon >= 12) mon = -1;
		}
		else if (argnmatch(token, "yr")) {
			year = atoi(val);
		}
		else if (argnmatch(token, "hour")) {
			hour = atoi(val);
		}
		else if (argnmatch(token, "min")) {
			min = atoi(val);
		}
		else if (argnmatch(token, "sec")) {
			sec = atoi(val);
		}

		token = strtok(NULL, "&");
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

	free(query);
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
					dprintf("rm %s\n", fn);
					unlink(fn);
				}
				else if (S_ISDIR(st.st_mode)) {
					dprintf("Cleaning directory %s\n", fn);
					cleandir(fn);
					dprintf("rmdir %s\n", fn);
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
	int i;
	pid_t childpid;
	int childstat;
	char htmldelim[20];
	char startstr[20];
	int argi, newargi;

	newargi = 0;
	bbgen_argv[newargi++] = bbgencmd;
	bbgen_argv[newargi++] = bbgentimeopt;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
		else {
			bbgen_argv[newargi++] = argv[argi];
		}
	}
	bbgen_argv[newargi++] = outdir;
	bbgen_argv[newargi++] = NULL;

	envcheck(reqenv);
	parse_query();

	/*
	 * Need to set these up AFTER putting them into bbgen_argv, since we
	 * need to have option parsing done first.
	 */
	if (getenv("BBGEN")) sprintf(bbgencmd, "%s", getenv("BBGEN"));
	else sprintf(bbgencmd, "%s/bin/bbgen", getenv("BBHOME"));

	sprintf(bbgentimeopt, "--snapshot=%u", (unsigned int)starttime);

	sprintf(dirid, "%u-%u", (unsigned int)getpid(), (unsigned int)time(NULL));
	sprintf(outdir, "%s/%s", getenv("BBSNAP"), dirid);
	if (mkdir(outdir, 0755) == -1) errormsg("Cannot create output directory");

	sprintf(bbwebenv, "BBWEB=%s/%s", getenv("BBSNAPURL"), dirid);
	putenv(bbwebenv);

	/* Output the "please wait for report ... " thing */
	sprintf(htmldelim, "bbrep-%u-%u", (int)getpid(), (unsigned int)time(NULL));
	printf("Content-type: multipart/mixed;boundary=%s\n", htmldelim);
	printf("\n");
	printf("%s\n", htmldelim);
	printf("Content-type: text/html\n\n");

	/* It's ok with these hardcoded values, as they are not used for this page */
	sethostenv("", "", "", colorname(COL_BLUE));
	sethostenv_report(starttime, starttime, 97.0, 99.995);
	headfoot(stdout, "bbrep", "", "header", COL_BLUE);

	strftime(startstr, sizeof(startstr), "%b %d %Y", localtime(&starttime));
	printf("<CENTER><A NAME=begindata>&nbsp;</A>\n");
	printf("<BR><BR><BR><BR>\n");
	printf("<H3>Generating snapshot: %s<BR>\n", startstr);
	printf("<P><P>\n");
	fflush(stdout);


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
			printf("%s\n\n", htmldelim);
			printf("Content-Type: text/html\n\n");
			errormsg("Could not generate report");
		}
		else {
			/* Send the browser off to the report */
			printf("Done...<P></BODY></HTML>\n");
			fflush(stdout);
			printf("%s\n\n", htmldelim);
			printf("Content-Type: text/html\n\n");
			printf("<HTML><HEAD>\n");
			printf("<META HTTP-EQUIV=\"REFRESH\" CONTENT=\"0; URL=%s/%s/\"\n", 
					getenv("BBSNAPURL"), dirid);
			printf("</HEAD><BODY BGCOLOR=\"000000\"></BODY></HTML>\n");
			printf("\n%s\n", htmldelim);
			fflush(stdout);
		}

		cleandir(getenv("BBSNAP"));
	}
	else {
		printf("%s\n\n", htmldelim);
		printf("Content-Type: text/html\n\n");
		errormsg("Fork failed");
	}

	return 0;
}

