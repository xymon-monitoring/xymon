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

static char rcsid[] = "$Id: bb-eventlog.c,v 1.13 2005-01-20 10:45:44 henrik Exp $";

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

#include "bbgen.h"
#include "util.h"
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
	char eventlogfilename[PATH_MAX];
	time_t cutoff;
	event_t	*eventhead, *walk;
	struct stat st;
	char l[MAX_LINE_LEN];
	char title[200];

	havedoneeventlog = 1;

	cutoff = ( (maxminutes) ? (time(NULL) - maxminutes*60) : 0);
	if (!maxcount) maxcount = 100;

	sprintf(eventlogfilename, "%s/allevents", xgetenv("BBHIST"));
	eventlog = fopen(eventlogfilename, "r");
	if (!eventlog) {
		errprintf("Cannot open eventlog\n");
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
				unsigned int uicurtime;
				fseek(eventlog, -maxcount*80, SEEK_CUR); 
				fgets(l, sizeof(l), eventlog); /* Skip to start of line */
				fgets(l, sizeof(l), eventlog);
				sscanf(l, "%*s %*s %u %*u %*u %*s %*s %*d", &uicurtime);
				curtime = uicurtime;
				done = (curtime < cutoff);
			}
			else {
				rewind(eventlog);
				done = 1;
			}
		} while (!done);
	}
	
	eventhead = NULL;

	while (fgets(l, sizeof(l), eventlog)) {

		time_t eventtime, changetime, duration;
		unsigned int uievt, uicht, uidur;
		char hostname[MAX_LINE_LEN], svcname[MAX_LINE_LEN], newcol[MAX_LINE_LEN], oldcol[MAX_LINE_LEN];
		int state, itemsfound;
		event_t *newevent;
		struct host_t *eventhost;
		struct bbgen_col_t *eventcolumn;

		itemsfound = sscanf(l, "%s %s %u %u %u %s %s %d",
			hostname, svcname,
			&uievt, &uicht, &uidur, 
			newcol, oldcol, &state);
		eventtime = uievt; changetime = uicht; duration = uidur;

		eventhost = find_host(hostname);
		eventcolumn = find_or_create_column(svcname, 1);

		if ( (itemsfound == 8) && 
		     (eventtime > cutoff) && 
		     (allowallhosts || (eventhost && !eventhost->nobb2)) && 
		     (wanted_eventcolumn(svcname)) ) {

			newevent = (event_t *) malloc(sizeof(event_t));
			newevent->host       = eventhost;
			newevent->service    = eventcolumn;
			newevent->eventtime  = eventtime;
			newevent->changetime = changetime;
			newevent->duration   = duration;
			newevent->newcolor   = eventcolor(newcol);
			newevent->oldcolor   = eventcolor(oldcol);

			newevent->next = eventhead;
			eventhead = newevent;
		}
	}

	if (eventhead) {
		char *bgcolors[2] = { "000000", "000033" };
		int  bgcolor = 0;
		int  count;
		struct event_t *lasttoshow = eventhead;

		count=0;
		walk=eventhead; 
		do {
			count++;
			lasttoshow = walk;
			walk = walk->next;
		} while (walk && (count<maxcount));

		sprintf(title, "%d events received in the past %u minutes",
			count, (unsigned int)((time(NULL) - lasttoshow->eventtime) / 60));

		fprintf(output, "<BR><BR>\n");
        	fprintf(output, "<TABLE SUMMARY=\"$EVENTSTITLE\" BORDER=0>\n");
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);

		for (walk=eventhead; (walk != lasttoshow->next); walk=walk->next) {
			fprintf(output, "<TR BGCOLOR=%s>\n", bgcolors[bgcolor]);
			bgcolor = ((bgcolor + 1) % 2);

			fprintf(output, "<TD ALIGN=CENTER>%s</TD>\n", ctime(&walk->eventtime));

			if (walk->newcolor == COL_CLEAR) {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=black><FONT COLOR=white>%s</FONT></TD>\n",
					walk->host->hostname);
			}
			else {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n",
					colorname(walk->newcolor), walk->host->hostname);
			}

			fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", walk->service->name);
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(walk->host->hostname, walk->service->name, walk->changetime));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=\"%s\" TITLE=\"%s\"></A>\n", 
				xgetenv("BBSKIN"), dotgiffilename(walk->oldcolor, 0, 0), 
				xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"), 
				colorname(walk->oldcolor), colorname(walk->oldcolor));
			fprintf(output, "<IMG SRC=\"%s/arrow.gif\" BORDER=0 ALT=\"From -&gt; To\">\n", 
				xgetenv("BBSKIN"));
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(walk->host->hostname, walk->service->name, walk->eventtime));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=\"%s\" TITLE=\"%s\"></A>\n", 
				xgetenv("BBSKIN"), dotgiffilename(walk->newcolor, 0, 0), 
				xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"), 
				colorname(walk->newcolor), colorname(walk->newcolor));
		}

		fprintf(output, "</TABLE>\n");

		/* Clean up */
		walk = eventhead;
		do {
			struct event_t *tmp = walk;

			walk = walk->next;
			xfree(tmp);
		} while (walk);
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

		token = strtok(NULL, "&");
	}

	xfree(query);
}

int main(int argc, char *argv[])
{
	int argi;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
	}

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

