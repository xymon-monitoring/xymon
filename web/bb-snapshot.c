/*----------------------------------------------------------------------------*/
/* Hobbit "snapshot" CGI front-end.                                           */
/*                                                                            */
/* This is a CGI front-end that lets the user pick which snapshot view is     */
/* generated. A snapshot is a view of the Hobbit webpages from any time in    */
/* the past, generated from the history logs.                                 */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-snapshot.c,v 1.16 2005-06-03 15:24:51 henrik Exp $";

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

	if (xgetenv("QUERY_STRING") == NULL) {
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
			char *errptr;

			mon = strtol(val, &errptr, 10) - 1;
			if (errptr == val) {
				for (mon=0; (monthnames[mon] && strcmp(val, monthnames[mon])); mon++) ;
				if (mon >= 12) mon = -1;
			}
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

	xfree(query);
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

	if ((xgetenv("QUERY_STRING") == NULL) || (strlen(xgetenv("QUERY_STRING")) == 0)) {
		/* Present the query form */
		int formfile;
		char formfn[PATH_MAX];

		sprintf(formfn, "%s/web/snapshot_form", xgetenv("BBHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1);
			read(formfile, inbuf, st.st_size);
			inbuf[st.st_size] = '\0';
			close(formfile);

			printf("Content-Type: text/html\n\n");
			sethostenv("", "", "", colorname(COL_BLUE));

			headfoot(stdout, "snapshot", "", "header", COL_BLUE);
			output_parsed(stdout, inbuf, COL_BLUE, "report", time(NULL));
			headfoot(stdout, "snapshot", "", "footer", COL_BLUE);

			xfree(inbuf);
		}
		return 0;
	}

	envcheck(reqenv);
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
			printf("Content-Type: text/html\n\n");
			errormsg("Could not generate report");
		}
		else {
			/* Send the browser off to the report */
			if (usemultipart) {
				printf("Done...<P></BODY></HTML>\n");
				fflush(stdout);
				printf("%s\n\n", htmldelim);
			}
			printf("Content-Type: text/html\n\n");
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
		printf("Content-Type: text/html\n\n");
		errormsg("Fork failed");
	}

	return 0;
}

