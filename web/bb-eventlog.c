/*----------------------------------------------------------------------------*/
/* Hobbit eventlog generator tool.                                            */
/*                                                                            */
/* This displays the "eventlog" found on the "All non-green status" page.     */
/* It also implements a CGI tool to show an eventlog for a given period of    */
/* time, as a reporting function.                                             */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/* Host/test/color/start/end filtering code by Eric Schwimmer 2005            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-eventlog.c,v 1.27 2006-01-13 12:51:35 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "libbbgen.h"

/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 * 	COUNT=50
 * 	MAXTIME=240
 *
 */

int	maxcount = 100;		/* Default: Include last 100 events */
int	maxminutes = 240;	/* Default: for the past 4 hours */
char	*totime = NULL;
char	*fromtime = NULL;
char	*hostregex = NULL;
char	*testregex = NULL;
char	*colrregex = NULL;
int	ignoredialups = 0;

char *reqenv[] = {
"BBHOSTS",
"BBHIST",
"BBSKIN",
"DOTWIDTH",
"DOTHEIGHT",
NULL };

static void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(void)
{
	char *query, *token;

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

		if (argnmatch(token, "MAXCOUNT")) {
			maxcount = atoi(val);
		}
		else if (argnmatch(token, "MAXTIME")) {
			maxminutes = atoi(val);
		}
		else if (argnmatch(token, "FROMTIME")) {
			if (*val) fromtime = strdup(val);
		}
		else if (argnmatch(token, "TOTIME")) {
			if (*val) totime = strdup(val);
		}
		else if (argnmatch(token, "HOSTMATCH")) {
			if (*val) hostregex = strdup(val);
		}
		else if (argnmatch(token, "TESTMATCH")) {
			if (*val) testregex = strdup(val);
		}
		else if (argnmatch(token, "COLORMATCH")) {
			if (*val) colrregex = strdup(val);
		}
		else if (argnmatch(token, "NODIALUPS")) {
			ignoredialups = 1;
		}

		token = strtok(NULL, "&");
	}

	xfree(query);
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
	}

	redirect_cgilog("bb-eventlog");

	if ((xgetenv("QUERY_STRING") == NULL) || (strlen(xgetenv("QUERY_STRING")) == 0)) {
		/* Present the query form */
		int formfile;
		char formfn[PATH_MAX];

		sprintf(formfn, "%s/web/event_form", xgetenv("BBHOME"));
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
			sethostenv("", "", "", colorname(COL_BLUE), NULL);

			headfoot(stdout, "event", "", "header", COL_BLUE);
			output_parsed(stdout, inbuf, COL_BLUE, "report", time(NULL));
			headfoot(stdout, "event", "", "footer", COL_BLUE);

			xfree(inbuf);
		}
		return 0;
	}

	envcheck(reqenv);
	parse_query();
	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());

	/* Now generate the webpage */
	printf("Content-Type: text/html\n\n");

	headfoot(stdout, "event", "", "header", COL_GREEN);
	fprintf(stdout, "<center>\n");
	do_eventlog(stdout, maxcount, maxminutes, fromtime, totime, hostregex, testregex, colrregex, ignoredialups);
	fprintf(stdout, "</center>\n");
	headfoot(stdout, "event", "", "footer", COL_GREEN);

	return 0;
}

