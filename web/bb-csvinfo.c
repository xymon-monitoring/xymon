/*----------------------------------------------------------------------------*/
/* Hobbit general CSV info viewer.                                            */
/*                                                                            */
/* This is a CGI script for a generic presentation of information stored in   */
/* a comma-separated file (CSV), e.g. via an export from a spreadsheet or DB. */
/* It is also used for the Hobbit column-name links, to provide information   */
/* about what each column header means and what kind of test is run.          */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-csvinfo.c,v 1.12 2005-03-22 09:03:37 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#include "libbbgen.h"

#define MAXCOLUMNS 80

char *srcdb = "hostinfo.csv";
char *wantedname = "";
int keycolumn = 0;
char delimiter = ';';


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

        if (xgetenv("QUERY_STRING") == NULL) {
                errormsg("Missing request");
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

		if (argnmatch(token, "key")) {
			wantedname = strdup(val);
		}
		else if (argnmatch(token, "db")) {
			char *p;

			/* Dont allow any slashes in the db-name */
			p = strrchr(val, '/');
			if (p) val = (p+1);

			srcdb = strdup(val);
		}
		else if (argnmatch(token, "column")) {
			keycolumn = atoi(val);
		}
		else if (argnmatch(token, "delimiter")) {
			delimiter = *val;
		}
		token = strtok(NULL, "&");
	}
}


int main(int argc, char *argv[])
{
	FILE *db;
	char dbfn[PATH_MAX];
	char buf[MAX_LINE_LEN];
	char *hffile = "info";
	int bgcolor = COL_BLUE;

	char *headers[MAXCOLUMNS];
	char *items[MAXCOLUMNS];

	int i, found;
	int argi;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--hffile=")) {
			char *p = strchr(argv[argi], '=');
			hffile = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--color=")) {
			char *p = strchr(argv[argi], '=');
			bgcolor = parse_color(p+1);
		}
	}

	parse_query();
	if (strlen(wantedname) == 0) {
		errormsg("Invalid request");
		return 1;
	}

	sprintf(dbfn, "%s/etc/%s", xgetenv("BBHOME"), srcdb);
	db = fopen(dbfn, "r");
	if (db == NULL) {
		char msg[PATH_MAX];

		sprintf(msg, "Cannot open sourcedb %s\n", dbfn);
		errormsg(msg);
		return 1;
	}

	/* First, load the headers from line 1 of the sourcedb */
	memset(headers, 0, sizeof(headers));
	if (fgets(buf, sizeof(buf), db)) {
		char *p1, *p2;

		for (i=0, p1=buf, p2=strchr(buf, delimiter); (p1 && p2 && strlen(p1)); i++,p1=p2+1,p2=strchr(p1, delimiter)) {
			*p2 = '\0';
			headers[i] = strdup(p1);
		}
	}


	/*
	 * Pre-allocate the buffer space for the items - we weill be stuffing data
	 * into these while scanning for the right item.
	 */
	for (i=0; i<MAXCOLUMNS; i++) items[i] = malloc(MAX_LINE_LEN);

	found = 0;
	while (!found && fgets(buf, sizeof(buf), db)) {

		char *p1, *p2;

		for (i=0; i<MAXCOLUMNS; i++) *(items[i]) = '\0';

		for (i=0, p1=buf, p2=strchr(buf, delimiter); (p1 && p2 && strlen(p1)); i++,p1=p2+1,p2=strchr(p1, delimiter)) {
			*p2 = '\0';
			strcpy(items[i], (strlen(p1) ? p1 : "&nbsp;"));
		}

		found = (strcasecmp(items[keycolumn], wantedname) == 0);
	}

	if (!found) {
		errormsg("No match");
		return 1;
	}


	/*
	 * Ready ... go build the webpage.
	 */
	printf("Content-Type: text/html\n\n");

        /* It's ok with these hardcoded values, as they are not used for this page */
        sethostenv(wantedname, "", "", colorname(bgcolor));
        headfoot(stdout, hffile, "", "header", bgcolor);

	printf("<table align=center border=1 summary=\"Information table\">\n");

	for (i=0; (headers[i]); i++) {
		printf("<tr>\n");
		printf("  <th align=left>%s</th><td align=left valign=top>%s</td>\n", headers[i], items[i]);
		printf("</tr>\n");
	}

	printf("</table>\n");
        headfoot(stdout, hffile, "", "footer", bgcolor);

	return 0;
}

