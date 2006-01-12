/*----------------------------------------------------------------------------*/
/* Hobbit status report generator.                                            */
/*                                                                            */
/* This is a CGI program to generate a simple HTML table with a summary of    */
/* all FOO statuses for a group of hosts.                                     */
/*                                                                            */
/* E.g. this can generate a report of all SSL certificates that are about     */
/* to expire.                                                                 */
/*                                                                            */
/* Copyright (C) 2006      Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-statusreport.c,v 1.1 2006-01-12 12:41:08 henrik Exp $";

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{

	char *server = NULL;
	char *cookie, *p, *pagefilter = "";
	char *otherfilter = "";
	char *testname = "mbsa";
	int  showcolors = 1;
	int  allhosts = 0;
	char *req, *board, *l;
	int argi, res;

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
		else if (argnmatch(argv[argi], "--column=")) {
			char *p = strchr(argv[argi], '=');
			testname = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--test=")) {
			char *p = strchr(argv[argi], '=');
			testname = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--filter=")) {
			char *p = strchr(argv[argi], '=');
			otherfilter = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--show-colors") == 0) {
			showcolors = 1;
		}
		else if (strcmp(argv[argi], "--no-colors") == 0) {
			showcolors = 0;
		}
		else if (strcmp(argv[argi], "--all") == 0) {
			allhosts = 1;
		}
	}

	if (!allhosts) {
      		/* Setup the filter we use for the report */
		cookie = getenv("HTTP_COOKIE");
		if (cookie && ((p = strstr(cookie, "pagepath=")) != NULL)) {
			p += strlen("pagepath=");
			pagefilter = malloc(strlen(p) + 6);
			sprintf(pagefilter, "page=%s", p);
			p = strchr(pagefilter, ';'); if (p) *p = '\0';
			if (strlen(pagefilter) == 0) { xfree(pagefilter); pagefilter = ""; }
		}
	}

	init_timestamp();
	req = malloc(1024 + strlen(testname) + strlen(pagefilter) + strlen(otherfilter));
	sprintf(req, "hobbitdboard test=%s fields=hostname,color,msg %s %s",
		testname, pagefilter, otherfilter);
	res = sendmessage(req, server, NULL, &board, 1, BBTALK_TIMEOUT);

	if (res != BB_OK) return 1;

	printf("Content-type: text/html\n\n");

	printf("<html><head><title>%s report on %s</title></head>\n", testname, timestamp);
	printf("<body><table border=1 cellpadding=5px><tr><th>Host</th><th align=left>Status</th></tr>\n");
	l = board;
	while (l && *l) {
		char *host, *colorstr = NULL, *msg = NULL, *p;
		char *eoln = strchr(l, '\n');
		if (eoln) *eoln = '\0';

		host = l;
		p = strchr(l, '|'); if (p) { *p = '\0'; l = colorstr = p+1; }
		p = strchr(l, '|'); if (p) { *p = '\0'; l = msg = p+1; }
		if (host && colorstr&& msg) {
			char *msgeol;

			nldecode(msg);
			msgeol = strchr(msg, '\n');
			if (msgeol) {
				/* Skip the first status line */
				msg = msgeol + 1;
			}
			printf("<tr><td align=left valign=top><b>%s", host);
			if (showcolors) {
				printf("&nbsp;-&nbsp;%s", colorstr);
			}
			printf("</b></td>\n");
			printf("<td><pre>\n");
			printf("%s", msg);
			printf("</pre></td></tr>\n");
		}

		if (eoln) l = eoln+1; else l = NULL;
	}
	printf("</table></body></html>\n");

	return 0;
}

