/*----------------------------------------------------------------------------*/
/* Hobbit eventlog generator tool.                                            */
/*                                                                            */
/* This displays the "eventlog" found on the "All non-green status" page.     */
/* It also implements a CGI tool to show an eventlog for a given period of    */
/* time, as a reporting function.                                             */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/* Host/test/color/start/end filtering code by Eric Schwimmer 2005            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-eventlog.c,v 1.37 2007-07-25 13:27:43 henrik Exp $";

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

int	maxcount = 100;		/* Default: Include last 100 events */
int	maxminutes = 240;	/* Default: for the past 4 hours */
char	*totime = NULL;
char	*fromtime = NULL;
char	*hostregex = NULL;
char	*exhostregex = NULL;
char	*testregex = NULL;
char	*extestregex = NULL;
char	*pageregex = NULL;
char	*expageregex = NULL;
char	*colorregex = NULL;
int	ignoredialups = 0;
int	topcount = 0;
char	*webfile_hf = "event";
char	*webfile_form = "event_form";
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
		else if (strcasecmp(cwalk->name, "COLORMATCH") == 0) {
			if (*(cwalk->value)) colorregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NODIALUPS") == 0) {
			ignoredialups = 1;
		}

		cwalk = cwalk->next;
	}
}

void show_topchanges(FILE *output, 
		     countlist_t *hostcounthead, countlist_t *svccounthead, event_t *eventhead, 
		     int topcount, time_t firstevent, time_t lastevent)
{
	if (fromtime || totime) {
		if (fromtime && totime) fprintf(output, "<p><font size=+1>Events between %s - %s</font></p>\n", fromtime, totime);
		else if (fromtime) fprintf(output, "<p><font size=+1>Events since %s</font></p>\n", fromtime);
		else if (totime) fprintf(output, "<p><font size=+1>Events until %s</font></p>\n", totime);
	}

	fprintf(output, "<table summary=\"Top changing hosts and services\" border=1>\n");
	fprintf(output, "<tr>\n");
	if (hostcounthead && (output != NULL)) {
		countlist_t *cwalk;
		int i;
		unsigned long others = 0;
		strbuffer_t *s = newstrbuffer(0);
		strbuffer_t *othercriteria = newstrbuffer(0);

		if (hostregex) {
			addtobuffer(othercriteria, "&amp;HOSTMATCH=");
			addtobuffer(othercriteria, hostregex);
		}
		if (exhostregex) addtobuffer(s, exhostregex);
		if (testregex) {
			addtobuffer(othercriteria, "&amp;TESTMATCH=");
			addtobuffer(othercriteria, testregex);
		}
		if (extestregex) {
			addtobuffer(othercriteria, "&amp;EXTESTMATCH=");
			addtobuffer(othercriteria, extestregex);
		}
		if (pageregex) {
			addtobuffer(othercriteria, "&amp;PAGEMATCH=");
			addtobuffer(othercriteria, pageregex);
		}
		if (expageregex) {
			addtobuffer(othercriteria, "&amp;EXPAGEMATCH=");
			addtobuffer(othercriteria, expageregex);
		}
		if (colorregex) {
			addtobuffer(othercriteria, "&amp;COLORMATCH=");
			addtobuffer(othercriteria, colorregex);
		}
		if (ignoredialups) {
			addtobuffer(othercriteria, "&amp;NODIALUPS=on");
		}

		fprintf(output, "<td width=40%% align=center valign=top>\n");
		fprintf(output, "   <table summary=\"Top %d hosts\" border=0>\n", topcount);
		fprintf(output, "      <tr><th colspan=2>Top %d hosts</th></tr>\n", topcount);
		fprintf(output, "      <tr><th align=left>Host</th><th align=left>State changes</th></tr>\n");

		for (i=0, cwalk=hostcounthead; (cwalk); i++, cwalk=cwalk->next) {
			if (i < topcount) {
				fprintf(output, "      <tr><td align=left><a href=\"bb-eventlog.sh?HOSTMATCH=^%s$&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</a></td><td align=right>%lu</td></tr>\n", 
					bbh_item(cwalk->src, BBH_HOSTNAME), 
					(unsigned long)firstevent, (unsigned long)lastevent,
					STRBUF(othercriteria),
					bbh_item(cwalk->src, BBH_HOSTNAME), cwalk->total);
				if (STRBUFLEN(s) > 0) addtobuffer(s, "|"); 
				addtobuffer(s, "^");
				addtobuffer(s, bbh_item(cwalk->src, BBH_HOSTNAME));
				addtobuffer(s, "$");
			}
			else {
				others += cwalk->total;
			}
		}
		fprintf(output, "      <tr><td align=left><a href=\"bb-eventlog.sh?EXHOSTMATCH=%s&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</a></td><td align=right>%lu</td></tr>\n", 
			STRBUF(s),
			(unsigned long)firstevent, (unsigned long)lastevent,
			STRBUF(othercriteria),
			"Other hosts", others);
		fprintf(output, "   </table>\n");
		fprintf(output, "</td>\n");

		freestrbuffer(s);
		freestrbuffer(othercriteria);
	}
	if (svccounthead && (output != NULL)) {
		countlist_t *cwalk;
		int i;
		unsigned long others = 0;
		strbuffer_t *s = newstrbuffer(0);
		strbuffer_t *othercriteria = newstrbuffer(0);

		if (hostregex) {
			addtobuffer(othercriteria, "&amp;HOSTMATCH=");
			addtobuffer(othercriteria, hostregex);
		}
		if (exhostregex) {
			addtobuffer(othercriteria, "&amp;EXHOSTMATCH=");
			addtobuffer(othercriteria, exhostregex);
		}
		if (testregex) {
			addtobuffer(othercriteria, "&amp;TESTMATCH=");
			addtobuffer(othercriteria, testregex);
		}
		if (extestregex) addtobuffer(s, extestregex);
		if (pageregex) {
			addtobuffer(othercriteria, "&amp;PAGEMATCH=");
			addtobuffer(othercriteria, pageregex);
		}
		if (expageregex) {
			addtobuffer(othercriteria, "&amp;EXPAGEMATCH=");
			addtobuffer(othercriteria, expageregex);
		}
		if (colorregex) {
			addtobuffer(othercriteria, "&amp;COLORMATCH=");
			addtobuffer(othercriteria, colorregex);
		}
		if (ignoredialups) {
			addtobuffer(othercriteria, "&amp;NODIALUPS=on");
		}


		fprintf(output, "<td width=40%% align=center valign=top>\n");
		fprintf(output, "   <table summary=\"Top %d services\" border=0>\n", topcount);
		fprintf(output, "      <tr><th colspan=2>Top %d services</th></tr>\n", topcount);
		fprintf(output, "      <tr><th align=left>Service</th><th align=left>State changes</th></tr>\n");

		for (i=0, cwalk=svccounthead; (cwalk); i++, cwalk=cwalk->next) {
			if (i < topcount) {
				fprintf(output, "      <tr><td align=left><a href=\"bb-eventlog.sh?TESTMATCH=^%s$&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</td><td align=right>%lu</td></tr>\n", 
					((htnames_t *)cwalk->src)->name, 
					(unsigned long)firstevent, (unsigned long)lastevent,
					STRBUF(othercriteria),
					((htnames_t *)cwalk->src)->name, cwalk->total);
				if (STRBUFLEN(s) > 0) addtobuffer(s, "|"); 
				addtobuffer(s, "^");
				addtobuffer(s, ((htnames_t *)cwalk->src)->name);
				addtobuffer(s, "$");
			}
			else {
				others += cwalk->total;
			}
		}
		fprintf(output, "      <tr><td align=left><a href=\"bb-eventlog.sh?EXTESTMATCH=%s&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</td><td align=right>%lu</td></tr>\n", 
			STRBUF(s),
			(unsigned long)firstevent, (unsigned long)lastevent,
			STRBUF(othercriteria),
			"Other services", others);
		fprintf(output, "   </table>\n");
		fprintf(output, "</td>\n");

		freestrbuffer(s);
		freestrbuffer(othercriteria);
	}
	fprintf(output, "</tr>\n");
	fprintf(output, "</table>\n");
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
		else if (argnmatch(argv[argi], "--top=")) {
			char *p = strchr(argv[argi], '=');
			topcount = atoi(p+1);
			webfile_hf = "topchanges";
			webfile_form = "topchanges_form";
			maxminutes = -1;
			maxcount = -1;
		}
	}

	redirect_cgilog("bb-eventlog");

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		showform(stdout, webfile_hf, webfile_form, COL_BLUE, getcurrenttime(NULL), NULL, NULL);
		return 0;
	}

	parse_query();
	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());

	/* Now generate the webpage */
	headfoot(stdout, webfile_hf, "", "header", COL_GREEN);
	fprintf(stdout, "<center>\n");

	if (topcount == 0) {
		do_eventlog(stdout, maxcount, maxminutes, fromtime, totime, 
			    pageregex, expageregex, hostregex, exhostregex, testregex, extestregex,
			    colorregex, ignoredialups, NULL,
			    NULL, NULL, NULL);
	}
	else {
		countlist_t *hcounts, *scounts;
		event_t *events, *ewalk;
		time_t firstevent, lastevent;

		do_eventlog(NULL, -1, -1, fromtime, totime, 
			    pageregex, expageregex, hostregex, exhostregex, testregex, extestregex,
			    colorregex, ignoredialups, NULL,
			    &events, &hcounts, &scounts);

		if (events) {
			lastevent = events->eventtime;
			ewalk = events; while (ewalk->next) ewalk = ewalk->next;
			firstevent = ewalk->eventtime;
		}
		show_topchanges(stdout, hcounts, scounts, events, topcount, firstevent, lastevent);
	}

	fprintf(stdout, "</center>\n");
	headfoot(stdout, webfile_hf, "", "footer", COL_GREEN);

	return 0;
}

