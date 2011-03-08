/*----------------------------------------------------------------------------*/
/* Hobbit status report generator.                                            */
/*                                                                            */
/* This is a CGI program to generate a simple HTML table with a summary of    */
/* all FOO statuses for a group of hosts.                                     */
/*                                                                            */
/* E.g. this can generate a report of all SSL certificates that are about     */
/* to expire.                                                                 */
/*                                                                            */
/* Copyright (C) 2006-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	char *envarea = NULL;
	char *server = NULL;
	char *cookie, *pagefilter = "";
	char *filter = NULL;
	char *heading = NULL;
	int  showcolors = 1;
	int  showcolumn = 0;
	int  addlink = 0;
	int  allhosts = 0;
	int  summary = 0;
	int  embedded = 0;
	char *req, *board, *l;
	int argi, res;
	sendreturn_t *sres;

	init_timestamp();
	for (argi=1; (argi < argc); argi++) {
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
		else if ( argnmatch(argv[argi], "--column=") || argnmatch(argv[argi], "--test=")) {
			char *p = strchr(argv[argi], '=');
			int needed;
			
			needed = 10 + strlen(p); if (filter) needed += strlen(filter);
			filter = (char *)(filter ? realloc(filter, needed) : calloc(1, needed));
			sprintf(filter + strlen(filter), " test=%s", p+1);

			if (!heading) {
				heading = (char *)malloc(1024 + strlen(p));
				sprintf(heading, "%s report on %s", p+1, timestamp);
			}
		}
		else if (argnmatch(argv[argi], "--filter=")) {
			char *p = strchr(argv[argi], '=');
			int needed;
			
			needed = 10 + strlen(p); if (filter) needed += strlen(filter);
			filter = (char *)(filter ? realloc(filter, needed) : calloc(1, needed));
			sprintf(filter + strlen(filter), " %s", p+1);
		}
		else if (argnmatch(argv[argi], "--heading=")) {
			char *p = strchr(argv[argi], '=');

			heading = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--show-column") == 0) {
			showcolumn = 1;
		}
		else if (strcmp(argv[argi], "--show-test") == 0) {
			showcolumn = 1;
		}
		else if (strcmp(argv[argi], "--show-colors") == 0) {
			showcolors = 1;
		}
		else if (strcmp(argv[argi], "--show-summary") == 0) {
			summary = 1;
		}
		else if (strcmp(argv[argi], "--show-message") == 0) {
			summary = 0;
		}
		else if (strcmp(argv[argi], "--link") == 0) {
			addlink = 1;
		}
		else if (strcmp(argv[argi], "--no-colors") == 0) {
			showcolors = 0;
		}
		else if (strcmp(argv[argi], "--all") == 0) {
			allhosts = 1;
		}
		else if (strcmp(argv[argi], "--embedded") == 0) {
			embedded = 1;
		}
	}

	if (!allhosts) {
      		/* Setup the filter we use for the report */
		cookie = get_cookie("pagepath");
		if (cookie && *cookie) {
			pagefilter = malloc(10 + 2*strlen(cookie));
			sprintf(pagefilter, "page=^%s$|^%s/.+", cookie, cookie);
		}
	}

	sres = newsendreturnbuf(1, NULL);
	req = malloc(1024 + strlen(pagefilter) + strlen(filter));
	sprintf(req, "hobbitdboard fields=hostname,testname,color,msg %s %s",
		pagefilter, filter);
	res = sendmessage(req, server, BBTALK_TIMEOUT, sres);
	board = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);

	if (res != BB_OK) return 1;

	if (!embedded) {
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

		printf("<html><head><title>%s</title></head>\n", heading);
		printf("<body>");
		printf("<table border=1 cellpadding=5><tr><th>%s</th><th align=left>Status</th></tr>\n",
		       (showcolumn ? "Host/Column" : "Host"));
	}

	l = board;
	while (l && *l) {
		char *hostname, *testname = NULL, *colorstr = NULL, *msg = NULL, *p;
		char *eoln = strchr(l, '\n');
		if (eoln) *eoln = '\0';

		hostname = l;
		p = strchr(l, '|'); if (p) { *p = '\0'; l = testname = p+1; }
		p = strchr(l, '|'); if (p) { *p = '\0'; l = colorstr = p+1; }
		p = strchr(l, '|'); if (p) { *p = '\0'; l = msg = p+1; }
		if (hostname && testname && colorstr && msg) {
			char *msgeol;

			nldecode(msg);
			msgeol = strchr(msg, '\n');
			if (msgeol) {
				/* Skip the first status line */
				msg = msgeol + 1;
			}
			printf("<tr><td align=left valign=top><b>");

			if (addlink) 
				printf("<a href=\"%s\">%s</a>", hostsvcurl(hostname, xgetenv("INFOCOLUMN"), 1), hostname);
			else 
				printf("%s", hostname);

			if (showcolumn) {
				printf("<br>");
				if (addlink) 
					printf("<a href=\"%s\">%s</a>", hostsvcurl(hostname, testname, 1), testname);
				else
					printf("%s", testname);
			}

			if (showcolors) printf("&nbsp;-&nbsp;%s", colorstr);

			printf("</b></td>\n");

			printf("<td><pre>\n");

			if (summary) {
				int firstline = 1;
				char *bol, *eol;

				bol = msg;
				while (bol) {
					eol = strchr(bol, '\n'); if (eol) *eol = '\0';

					if (firstline) {
						if (!isspace((int)*bol)) {
							printf("%s\n", bol);
							firstline = 0;
						}
					}
					else if ((*bol == '&') && (strncmp(bol, "&green", 6) != 0)) {
						printf("%s\n", bol);
					}

					bol = (eol ? eol+1 : NULL);
				}
			}
			else {
				printf("%s", msg);
			}

			printf("</pre></td></tr>\n");
		}

		if (eoln) l = eoln+1; else l = NULL;
	}

	if (!embedded) printf("</table></body></html>\n");

	return 0;
}

