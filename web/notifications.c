/*----------------------------------------------------------------------------*/
/* Xymon notification log viewer                                              */
/*                                                                            */
/* Copyright (C) 2007-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

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

#include "libxymon.h"

int	maxcount = 100;		/* Default: Include last 100 events */
int	maxminutes = 1440;	/* Default: for the past 24 hours */
char	*totime = NULL;
char	*fromtime = NULL;
char	*hostregex = NULL;
char	*exhostregex = NULL;
char	*testregex = NULL;
char	*extestregex = NULL;
char	*pageregex = NULL;
char	*expageregex = NULL;
char	*rcptregex = NULL;
char	*exrcptregex = NULL;
cgidata_t *cgidata = NULL;

static void parse_query(void)
{
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "MAXCOUNT") == 0) {
			maxcount = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "MAXTIME") == 0) {
			maxminutes = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "FROMTIME") == 0) {
			if (*(cwalk->value)) fromtime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "TOTIME") == 0) {
			if (*(cwalk->value)) totime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "HOSTMATCH") == 0) {
			if (*(cwalk->value)) hostregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXHOSTMATCH") == 0) {
			if (*(cwalk->value)) exhostregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "TESTMATCH") == 0) {
			if (*(cwalk->value)) testregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXTESTMATCH") == 0) {
			if (*(cwalk->value)) extestregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "PAGEMATCH") == 0) {
			if (*(cwalk->value)) pageregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXPAGEMATCH") == 0) {
			if (*(cwalk->value)) expageregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "RCPTMATCH") == 0) {
			if (*(cwalk->value)) rcptregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXRCPTMATCH") == 0) {
			if (*(cwalk->value)) exrcptregex = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}

int main(int argc, char *argv[])
{
	int argi;

	libxymon_init(argv[0]);
	for (argi = 1; (argi < argc); argi++) {
		if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
	}

	redirect_cgilog(programname);
	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		showform(stdout, "notify", "notify_form", COL_BLUE, getcurrenttime(NULL), NULL, NULL);
		return 0;
	}

	parse_query();

	/* Now generate the webpage */
	headfoot(stdout, "notify", "", "header", COL_GREEN);
	fprintf(stdout, "<center>\n");
	do_notifylog(stdout, maxcount, maxminutes, fromtime, totime, 
			pageregex, expageregex, 
			hostregex, exhostregex, 
			testregex, extestregex,
			rcptregex, exrcptregex);
	fprintf(stdout, "</center>\n");
	headfoot(stdout, "notify", "", "footer", COL_GREEN);

	return 0;
}

