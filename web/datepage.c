/*----------------------------------------------------------------------------*/
/* Xymon webpage generator tool.                                              */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>

#include "libxymon.h"
#include "version.h"

static enum { FRM_NONE, FRM_MONTH, FRM_WEEK, FRM_DAY } frmtype = FRM_NONE;
static int year = -1;
static int month = -1;
static int day = -1;
static int week = -1;

static void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;

	if (cgidata == NULL) {
		errormsg(cgi_error());
	}

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "YEAR") == 0) {
			year = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "MONTH") == 0) {
			month = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "DAY") == 0) {
			day = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "WEEK") == 0) {
			week = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "TYPE") == 0) {
			if (strcasecmp(cwalk->value, "month") == 0) frmtype = FRM_MONTH;
			else if (strcasecmp(cwalk->value, "week") == 0) frmtype = FRM_WEEK;
			else if (strcasecmp(cwalk->value, "day") == 0) frmtype = FRM_DAY;
			else errormsg("Bad type parameter\n");
		}

		cwalk = cwalk->next;
	}
}

int main(int argc, char *argv[])
{
	int argi;
	char *hffile = "report";
	char *urlprefix = "";
	int bgcolor = COL_BLUE;
	char *envarea = NULL;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
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

	redirect_cgilog("datepage");
	parse_query();

	if (cgi_method == CGI_POST) {
		char *pagepath, *cookie, *endurl;

		cookie = get_cookie("pagepath");

		if (cookie && *cookie) {
			pagepath = strdup(cookie);
		}
		else {
			cookie = get_cookie("host");

			if (cookie && *cookie) {
				void *hinfo;

				load_hostinfo(cookie);
				hinfo = hostinfo(cookie);
				if (hinfo) {
					pagepath = xmh_item(hinfo, XMH_PAGEPATH);
				}
				else {
					pagepath = "";
				}
			}
			else {
				pagepath = "";
			}
		}

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

		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		fprintf(stdout, "<html><head><meta http-equiv=\"refresh\" content=\"0; URL=%s\"></head></html>\n", endurl);
	}
	else if (cgi_method == CGI_GET) {
                char formfn[PATH_MAX];
		time_t seltime;
		struct tm *seltm;

		seltime = getcurrenttime(NULL); seltm = localtime(&seltime);

                /* Present the query form */
		switch (frmtype) {
		  case FRM_DAY:
			seltm->tm_mday -= 1; seltime = mktime(seltm);
			sprintf(formfn, "%s_form_daily", hffile);
			break;

		  case FRM_WEEK:
			seltm->tm_mday -= 7; seltime = mktime(seltm);
			sprintf(formfn, "%s_form_weekly", hffile);
			break;

		  case FRM_MONTH:
			seltm->tm_mon -= 1; seltime = mktime(seltm);
			sprintf(formfn, "%s_form_monthly", hffile);
			break;

		  case FRM_NONE:
			errormsg("No report type defined");
		}

		sethostenv("", "", "", colorname(bgcolor), NULL);
		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, hffile, formfn, COL_BLUE, seltime, NULL, NULL);
	}

	return 0;
}

