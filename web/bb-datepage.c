/*----------------------------------------------------------------------------*/
/* Hobbit webpage generator tool.                                             */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-datepage.c,v 1.1 2005-04-06 20:44:28 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"
#include "version.h"

static enum { FRM_NONE, FRM_MONTH, FRM_WEEK, FRM_DAY } frmtype = FRM_NONE;
static int year = -1;
static int month = -1;
static int day = -1;
static int week = -1;

static void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(char *buf)
{
	char *query, *token;

	if (!buf) {
		if (xgetenv("QUERY_STRING") == NULL) return;

		query = urldecode("QUERY_STRING");
		if (!urlvalidate(query, NULL)) {
			errormsg("Invalid request");
			return;
		}
	}
	else {
		query = buf;
	}

	token = strtok(query, "&");
	while (token) {
		char *val;
		
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }

		if (strcasecmp(token, "YEAR") == 0) {
			year = atoi(val);
		}
		else if (strcasecmp(token, "MONTH") == 0) {
			month = atoi(val);
		}
		else if (strcasecmp(token, "DAY") == 0) {
			day = atoi(val);
		}
		else if (strcasecmp(token, "WEEK") == 0) {
			week = atoi(val);
		}
		else if (strcasecmp(token, "TYPE") == 0) {
			if (strcasecmp(val, "month") == 0) frmtype = FRM_MONTH;
			else if (strcasecmp(val, "week") == 0) frmtype = FRM_WEEK;
			else if (strcasecmp(val, "day") == 0) frmtype = FRM_DAY;
			else errormsg("Bad type parameter\n");
		}

		token = strtok(NULL, "&");
	}

	free(query);
}

static void get_post_data(void)
{
	char l[8192];

	while (fgets(l, sizeof(l), stdin)) {
		errprintf("Form input: %s\n", l);
		parse_query(l);
	}
}


int main(int argc, char *argv[])
{
	int argi;
	char *hffile = "report";
	char *urlprefix = "";
	int bgcolor = COL_BLUE;

	freopen("/tmp/debug.txt", "a", stderr);

	for (argi = 1; (argi < argc); argi++) {
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
		else if (argnmatch(argv[argi], "--url=")) {
			char *p = strchr(argv[argi], '=');
			urlprefix = strdup(p+1);
		}
	}

	if (strcmp(xgetenv("REQUEST_METHOD"), "POST") == 0) {
		char *cookie, *pagepath, *p;
		char *endurl;

		errprintf("Got a POST\n");
		get_post_data();

		cookie = getenv("HTTP_COOKIE");
		if (cookie == NULL) {
			errormsg("Cookies must be enabled\n");
			return 1;
		}

		errprintf("Cookie: %s\n", cookie);
		p = strstr(cookie, "pagepath=");
		if (p == NULL) {
			p = strstr(cookie, "host=");
			if (p == NULL) {
				pagepath = "";
			}
			else {
				char *hname;
				namelist_t *hinfo;

				hname = strdup(p + strlen("host="));
				p = strchr(hname, ';'); if (p) *p = '\0';

				load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
				hinfo = hostinfo(hname);
				if (hinfo) {
					pagepath = bbh_item(hinfo, BBH_PAGEPATH);
				}
				else {
					pagepath = "";
				}
			}
		}
		else {
			pagepath = strdup(p + strlen("pagepath="));
			p = strchr(pagepath, ';'); if (p) *p = '\0';
		}

		errprintf("pagepath is: %s\n", pagepath);
		endurl = (char *)malloc(strlen(urlprefix) + strlen(pagepath) + 1024);

		switch (frmtype) {
		  case FRM_DAY:
			if ((year == -1) || (month == -1) || (day == -1)) errormsg("Invalid day-request");
			sprintf(endurl, "%s/daily/%d/%02d/%02d/%s", urlprefix, year, month, day, pagepath);
			break;

		  case FRM_WEEK:
			if ((year == -1) || (week == -1)) errormsg("Invalid week-request");
			sprintf(endurl, "%s/weekly/%d/%02d/%s", urlprefix, year, week, pagepath);
			break;

		  case FRM_MONTH:
			if ((year == -1) || (month == -1)) errormsg("Invalid month-request");
			sprintf(endurl, "%s/monthly/%d/%02d/%s", urlprefix, year, month, pagepath);
			break;

		  case FRM_NONE:
			break;
		}

		if (*pagepath) strcat(endurl, "/");

		errprintf("endurl: %s\n", endurl);
		fprintf(stdout, "Location: %s\n\n", endurl);
	}
	else {
		char *mnames[] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		int i;

		parse_query(NULL);

		fprintf(stdout, "Content-type: text/html\n\n");
		headfoot(stdout, hffile, "", "header", bgcolor);

		fprintf(stdout, "<form method=\"post\" action=\"%s\"\n", xgetenv("REQUEST_URI"));

		switch (frmtype) {
		  case FRM_DAY:
			fprintf(stdout, "<select name=\"month\">\n");
			for (i=1; (i<=12); i++) {
				fprintf(stdout, "<option value=\"%02d\">%s\n", i, mnames[i]);
			}
			fprintf(stdout, "</select>\n");

			fprintf(stdout, "<select name=\"day\">\n");
			for (i=1; (i<=31); i++) {
				fprintf(stdout, "<option value=\"%02d\">%d\n", i, i);
			}
			fprintf(stdout, "</select>\n");

			fprintf(stdout, "<select name=\"year\">\n");
			for (i=1999; (i<=2009); i++) {
				fprintf(stdout, "<option value=\"%02d\">%d\n", i, i);
			}
			fprintf(stdout, "</select>\n");
			fprintf(stdout, "<input type=\"hidden\" NAME=\"type\" value=\"day\">\n");
			break;

		  case FRM_WEEK:
			fprintf(stdout, "<select name=\"week\">\n");
			for (i=1; (i<=53); i++) {
				fprintf(stdout, "<option value=\"%02d\">%d\n", i, i);
			}
			fprintf(stdout, "</select>\n");

			fprintf(stdout, "<select name=\"year\">\n");
			for (i=1999; (i<=2009); i++) {
				fprintf(stdout, "<option value=\"%02d\">%d\n", i, i);
			}
			fprintf(stdout, "</select>\n");
			fprintf(stdout, "<input type=\"hidden\" NAME=\"type\" value=\"week\">\n");
			break;

		  case FRM_MONTH:
			fprintf(stdout, "<select name=\"month\">\n");
			for (i=1; (i<=12); i++) {
				fprintf(stdout, "<option value=\"%02d\">%s\n", i, mnames[i]);
			}
			fprintf(stdout, "</select>\n");

			fprintf(stdout, "<select name=\"year\">\n");
			for (i=1999; (i<=2009); i++) {
				fprintf(stdout, "<option value=\"%02d\">%d\n", i, i);
			}
			fprintf(stdout, "</select>\n");
			fprintf(stdout, "<input type=\"hidden\" NAME=\"type\" value=\"month\">\n");
			break;

		  case FRM_NONE:
			fprintf(stdout, "No type, got QUERY_STRING=%s\n", xgetenv("QUERY_STRING"));
			break;
		}

		fprintf(stdout, "<INPUT TYPE=\"SUBMIT\" NAME=\"SUBMONTHLY\" VALUE=\"View Report\" ALT=\"View Report\">\n");
		fprintf(stdout, "</form>\n");

		headfoot(stdout, hffile, "", "footer", bgcolor);
	}

	return 0;
}

