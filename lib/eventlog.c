/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: eventlog.c,v 1.3 2003-11-16 08:01:50 henrik Exp $";

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

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "eventlog.h"


char *eventignorecolumns = NULL;
int havedoneeventlog = 0;

static int wanted_eventcolumn(char *service)
{
	char svc[100];
	int result;

	if (!eventignorecolumns || (strlen(service) > (sizeof(svc)-3))) return 1;

	sprintf(svc, ",%s,", service);
	result = (strstr(eventignorecolumns, svc) == NULL);

	return result;
}

void do_eventlog(FILE *output, int maxcount, int maxminutes, int allowallhosts)
{
	FILE *eventlog;
	char eventlogfilename[MAX_PATH];
	char hostname[MAX_LINE_LEN], svcname[MAX_LINE_LEN], newcol[MAX_LINE_LEN], oldcol[MAX_LINE_LEN];
	time_t cutoff;
	event_t	*events;
	int num, eventintime_count, itemsfound;
	struct stat st;
	char l[MAX_LINE_LEN];
	char title[200];

	havedoneeventlog = 1;

	cutoff = ( (maxminutes) ? (time(NULL) - maxminutes*60) : 0);
	if (!maxcount) maxcount = 100;

	sprintf(eventlogfilename, "%s/allevents", getenv("BBHIST"));
	eventlog = fopen(eventlogfilename, "r");
	if (!eventlog) {
		errprintf("Cannot open eventlog");
		return;
	}

	if (stat(eventlogfilename, &st) == 0) {
		time_t curtime;
		int done = 0;

		/* Find a spot in the eventlog file close to where the cutoff time is */
		fseek(eventlog, 0, SEEK_END);
		do {
			/* Go back maxcount*80 bytes - one entry is ~80 bytes */
			if (ftell(eventlog) > maxcount*80) {
				fseek(eventlog, -maxcount*80, SEEK_CUR); 
				fgets(l, sizeof(l), eventlog); /* Skip to start of line */
				fgets(l, sizeof(l), eventlog);
				sscanf(l, "%*s %*s %u %*u %*u %*s %*s %*d", (unsigned int *)&curtime);
				done = (curtime < cutoff);
			}
			else {
				rewind(eventlog);
				done = 1;
			}
		} while (!done);
	}
	
	events = (event_t *) malloc(maxcount*sizeof(event_t));
	for (num=0; (num < maxcount); num++) events[num].hostname = events[num].service = NULL;
	eventintime_count = num = 0;

	while (fgets(l, sizeof(l), eventlog)) {

		itemsfound = sscanf(l, "%s %s %u %u %u %s %s %d",
			hostname, svcname,
			(unsigned int *)&events[num].eventtime, 
			(unsigned int *)&events[num].changetime, 
			(unsigned int *)&events[num].duration, 
			newcol, oldcol, &events[num].state);

		if ( (itemsfound == 8) && 
		     (events[num].eventtime > cutoff) && 
		     (allowallhosts || find_host(hostname)) && 
		     (wanted_eventcolumn(svcname)) ) {
			if (events[num].hostname != NULL) free(events[num].hostname);
			if (events[num].service != NULL) free(events[num].service);
			events[num].hostname = malcop(hostname);
			events[num].service = malcop(svcname);
			events[num].newcolor = eventcolor(newcol);
			events[num].oldcolor = eventcolor(oldcol);
			eventintime_count++;

			num = (num + 1) % maxcount;
		}
	}

	if (eventintime_count > 0) {
		int firstevent, lastevent;
		char *bgcolors[2] = { "000000", "000033" };
		int  bgcolor = 0;

		if (eventintime_count <= maxcount) {
			firstevent = 0;
			lastevent = eventintime_count-1;
		}
		else {
			firstevent = num;
			lastevent = ( (num == 0) ? (maxcount-1) : (num-1));
			eventintime_count = maxcount;
		}

		sprintf(title, "%d events received in the past %u minutes",
			eventintime_count, (unsigned int)((time(NULL)-events[firstevent].eventtime) / 60));

		fprintf(output, "<BR><BR>\n");
        	fprintf(output, "<TABLE SUMMARY=\"$EVENTSTITLE\" BORDER=0>\n");
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);

		for (num = lastevent; (eventintime_count); eventintime_count--, num = ((num == 0) ? (maxcount-1) : (num-1)) ) {
			fprintf(output, "<TR BGCOLOR=%s>\n", bgcolors[bgcolor]);
			bgcolor = ((bgcolor + 1) % 2);

			fprintf(output, "<TD ALIGN=CENTER>%s</TD>\n", ctime(&events[num].eventtime));

			if (events[num].newcolor == COL_CLEAR) {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=black><FONT COLOR=white>%s</FONT></TD>\n",
					events[num].hostname);
			}
			else {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n",
					colorname(events[num].newcolor),
					events[num].hostname);
			}

			fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", events[num].service);
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(events[num].hostname, events[num].service, events[num].changetime));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=%s></A>\n", 
				getenv("BBSKIN"), dotgiffilename(events[num].oldcolor, 0, 0), 
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"), 
				colorname(events[num].oldcolor));
			fprintf(output, "<IMG SRC=\"%s/arrow.gif\" BORDER=0 ALT=\"From -&gt; To\">\n", 
				getenv("BBSKIN"));
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(events[num].hostname, events[num].service, events[num].eventtime));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=%s></A>\n", 
				getenv("BBSKIN"), dotgiffilename(events[num].newcolor, 0, 0), 
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"), 
				colorname(events[num].newcolor));
		}

		fprintf(output, "</TABLE>\n");
	}
	else {
		/* No events during the past maxminutes */
		sprintf(title, "No events received in the last %d minutes", maxminutes);

		fprintf(output, "<CENTER><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD>\n", title);
		fprintf(output, "</TR>\n");
		fprintf(output, "</TABLE>\n");
		fprintf(output, "</CENTER>\n");
	}

	for (num=0; (num < maxcount); num++) {
		if (events[num].hostname != NULL) free(events[num].hostname);
		if (events[num].service != NULL) free(events[num].service);
	}

	free(events);
	fclose(eventlog);
}


#ifdef CGI

/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 * 	COUNT=50
 * 	MAXTIME=240
 *
 */

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };


int	maxcount = 100;		/* Default: Include last 100 events */
int	maxminutes = 240;	/* Default: for the past 4 hours */

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

		if (argnmatch(token, "MAXCOUNT")) {
			maxcount = atoi(val);
		}
		else if (argnmatch(token, "MAXTIME")) {
			maxminutes = atoi(val);
		}

		token = strtok(NULL, "&");
	}

	free(query);
}

int main(int argc, char *argv[])
{
	envcheck(reqenv);
	parse_query();

	/* Now generate the webpage */
	printf("Content-Type: text/html\n\n");

	headfoot(stdout, "bb2", "", "header", COL_GREEN);
	fprintf(stdout, "<center>\n");
	do_eventlog(stdout, maxcount, maxminutes, 1);
	fprintf(stdout, "</center>\n");
	headfoot(stdout, "bb2", "", "footer", COL_GREEN);

	return 0;
}

#endif

