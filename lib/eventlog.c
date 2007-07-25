/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
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

static char rcsid[] = "$Id: eventlog.c,v 1.42 2007-07-25 20:02:06 henrik Exp $";

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

#include <pcre.h>

#include "libbbgen.h"

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

static time_t convert_time(char *timestamp)
{
	time_t event = 0;
	unsigned int year,month,day,hour,min,sec,count;
	struct tm timeinfo;

	if ((*timestamp) && (*(timestamp + strspn(timestamp, "0123456789")) == '\0'))
		return (time_t) atol(timestamp);

	count = sscanf(timestamp, "%u/%u/%u@%u:%u:%u",
		&year, &month, &day, &hour, &min, &sec);
	if(count != 6) {
		return -1;
	}
	if(year < 1970) {
		return 0;
	}
	else {
		memset(&timeinfo, 0, sizeof(timeinfo));
		timeinfo.tm_year  = year - 1900;
		timeinfo.tm_mon   = month - 1;
		timeinfo.tm_mday  = day;
		timeinfo.tm_hour  = hour;
		timeinfo.tm_min   = min;
		timeinfo.tm_sec   = sec;
		timeinfo.tm_isdst = -1;
		event = mktime(&timeinfo);		
	}

	return event;
}

int record_compare(void *a, void *b)
{
	/* Sort the countlist_t records in reverse */
	if (((countlist_t *)a)->total > ((countlist_t *)b)->total) return -1;
	else if (((countlist_t *)a)->total < ((countlist_t *)b)->total) return 1;
	else return 0;
}

void * record_getnext(void *a)
{
	return ((countlist_t *)a)->next;
}

void record_setnext(void *a, void *newval)
{
	((countlist_t *)a)->next = (countlist_t *)newval;
}

static htnames_t *namehead = NULL;
static htnames_t *getname(char *name, int createit)
{
	htnames_t *walk;

	for (walk = namehead; (walk && strcmp(walk->name, name)); walk = walk->next) ;
	if (walk || (!createit)) return walk;

	walk = (htnames_t *)malloc(sizeof(htnames_t));
	walk->name = strdup(name);
	walk->next = namehead;
	namehead = walk;

	return walk;
}


void do_eventlog(FILE *output, int maxcount, int maxminutes, char *fromtime, char *totime, 
		char *pageregex, char *expageregex,
		char *hostregex, char *exhostregex,
		char *testregex, char *extestregex,
		char *colrregex, int ignoredialups,
		f_hostcheck hostcheck,
		event_t **eventlist, countlist_t **hostcounts, countlist_t **servicecounts,
		eventsummary_t sumtype)
{
	FILE *eventlog;
	char eventlogfilename[PATH_MAX];
	time_t firstevent = 0;
	time_t lastevent = getcurrenttime(NULL);
	event_t	*eventhead, *walk;
	struct stat st;
	char l[MAX_LINE_LEN];
	char title[200];

	/* For the PCRE matching */
	const char *errmsg = NULL;
	int errofs = 0;
	pcre *pageregexp = NULL;
	pcre *expageregexp = NULL;
	pcre *hostregexp = NULL;
	pcre *exhostregexp = NULL;
	pcre *testregexp = NULL;
	pcre *extestregexp = NULL;
	pcre *colrregexp = NULL;
	void *hostwalk;
	countlist_t *hostcounthead = NULL, *svccounthead = NULL;

	havedoneeventlog = 1;

	if ((maxminutes > 0) && (fromtime || totime)) {
		fprintf(output, "<B>Only one time interval type is allowed!</B>");
		return;
	}

	if (fromtime) {
		firstevent = convert_time(fromtime);
		if(firstevent < 0) {
			fprintf(output,"<B>Invalid 'from' time: %s</B>", fromtime);
			return;
		}
	}
	else if (maxminutes == -1) {
		/* Unlimited number of minutes */
		firstevent = 0;
	}
	else if (maxminutes > 0) {
		firstevent = getcurrenttime(NULL) - maxminutes*60;
	}
	else {
		firstevent = getcurrenttime(NULL) - 86400;
	}

	if (totime) {
		lastevent = convert_time(totime);
		if (lastevent < 0) {
			fprintf(output,"<B>Invalid 'to' time: %s</B>", totime);
			return;
		}
		if (lastevent < firstevent) {
			fprintf(output,"<B>'to' time must be after 'from' time.</B>");
			return;
		}
	}

	if (!maxcount) maxcount = 100;

	if (pageregex && *pageregex) pageregexp = pcre_compile(pageregex, PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (expageregex && *expageregex) expageregexp = pcre_compile(expageregex, PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (hostregex && *hostregex) hostregexp = pcre_compile(hostregex, PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (exhostregex && *exhostregex) exhostregexp = pcre_compile(exhostregex, PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (testregex && *testregex) testregexp = pcre_compile(testregex, PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (extestregex && *extestregex) extestregexp = pcre_compile(extestregex, PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (colrregex && *colrregex) colrregexp = pcre_compile(colrregex, PCRE_CASELESS, &errmsg, &errofs, NULL);

	sprintf(eventlogfilename, "%s/allevents", xgetenv("BBHIST"));
	eventlog = fopen(eventlogfilename, "r");

	if (eventlog && (stat(eventlogfilename, &st) == 0)) {
		time_t curtime;
		int done = 0;
		int unlimited = (maxcount == -1);

		if (unlimited) maxcount = 1000;
		do {
			/* Find a spot in the eventlog file close to where the firstevent time is */
			fseeko(eventlog, 0, SEEK_END);
			do {
				/* Go back maxcount*80 bytes - one entry is ~80 bytes */
				if (ftello(eventlog) > maxcount*80) {
					unsigned int uicurtime;
					fseeko(eventlog, -maxcount*80, SEEK_CUR); 
					fgets(l, sizeof(l), eventlog); /* Skip to start of line */
					fgets(l, sizeof(l), eventlog);
					sscanf(l, "%*s %*s %u %*u %*u %*s %*s %*d", &uicurtime);
					curtime = uicurtime;
					done = (curtime < firstevent);
					if (unlimited && !done) maxcount += 1000;
				}
				else {
					off_t ofs;
					rewind(eventlog);
					ofs = ftello(eventlog);
					done = 1;
				}
			} while (!done);

			if (unlimited) unlimited = ((curtime > firstevent) && (ftello(eventlog) > 0));
		} while (unlimited);
	}
	
	eventhead = NULL;

	while (eventlog && (fgets(l, sizeof(l), eventlog))) {

		time_t eventtime, changetime, duration;
		unsigned int uievt, uicht, uidur;
		char hostname[MAX_LINE_LEN], svcname[MAX_LINE_LEN], newcol[MAX_LINE_LEN], oldcol[MAX_LINE_LEN];
		char *newcolname, *oldcolname;
		int state, itemsfound, pagematch, hostmatch, testmatch, colrmatch;
		event_t *newevent;
		void *eventhost;
		struct htnames_t *eventcolumn;
		int ovector[30];
		eventcount_t *countrec;

		itemsfound = sscanf(l, "%s %s %u %u %u %s %s %d",
			hostname, svcname,
			&uievt, &uicht, &uidur, 
			newcol, oldcol, &state);
		eventtime = uievt; changetime = uicht; duration = uidur;
		oldcolname = colorname(eventcolor(oldcol));
		newcolname = colorname(eventcolor(newcol));
		if (eventtime > lastevent) break;
		eventhost = hostinfo(hostname);
		eventcolumn = getname(svcname, 1);

		if ( (itemsfound == 8) && 
		     (eventtime >= firstevent) && 
		     (eventhost && !bbh_item(eventhost, BBH_FLAG_NOBB2)) && 
		     (wanted_eventcolumn(svcname)) ) {
			if (ignoredialups && bbh_item(eventhost, BBH_FLAG_DIALUP)) continue;
			if (hostcheck && (hostcheck(hostname) == 0)) continue;

			if (pageregexp) {
				char *pagename;

				pagename = bbh_item_multi(eventhost, BBH_PAGEPATH);
				pagematch = 0;
				while (!pagematch && pagename) {
					pagematch = (pcre_exec(pageregexp, NULL, pagename, strlen(pagename), 0, 0, 
							ovector, (sizeof(ovector)/sizeof(int))) >= 0);
					pagename = bbh_item_multi(NULL, BBH_PAGEPATH);
				}
			}
			else
				pagematch = 1;
			if (!pagematch) continue;

			if (expageregexp) {
				char *pagename;

				pagename = bbh_item_multi(eventhost, BBH_PAGEPATH);
				pagematch = 0;
				while (!pagematch && pagename) {
					pagematch = (pcre_exec(expageregexp, NULL, pagename, strlen(pagename), 0, 0, 
							ovector, (sizeof(ovector)/sizeof(int))) >= 0);
					pagename = bbh_item_multi(NULL, BBH_PAGEPATH);
				}
			}
			else
				pagematch = 0;
			if (pagematch) continue;

			if (hostregexp)
				hostmatch = (pcre_exec(hostregexp, NULL, hostname, strlen(hostname), 0, 0, 
						ovector, (sizeof(ovector)/sizeof(int))) >= 0);
			else
				hostmatch = 1;
			if (!hostmatch) continue;

			if (exhostregexp)
				hostmatch = (pcre_exec(exhostregexp, NULL, hostname, strlen(hostname), 0, 0, 
						ovector, (sizeof(ovector)/sizeof(int))) >= 0);
			else
				hostmatch = 0;
			if (hostmatch) continue;

			if (testregexp)
				testmatch = (pcre_exec(testregexp, NULL, svcname, strlen(svcname), 0, 0, 
						ovector, (sizeof(ovector)/sizeof(int))) >= 0);
			else
				testmatch = 1;
			if (!testmatch) continue;

			if (extestregexp)
				testmatch = (pcre_exec(extestregexp, NULL, svcname, strlen(svcname), 0, 0, 
						ovector, (sizeof(ovector)/sizeof(int))) >= 0);
			else
				testmatch = 0;
			if (testmatch) continue;

			if (colrregexp) {
				colrmatch = ( (pcre_exec(colrregexp, NULL, newcolname, strlen(newcolname), 0, 0,
							ovector, (sizeof(ovector)/sizeof(int))) >= 0) ||
					      (pcre_exec(colrregexp, NULL, oldcolname, strlen(oldcolname), 0, 0,
							ovector, (sizeof(ovector)/sizeof(int))) >= 0) );
			}
			else
				colrmatch = 1;
			if (!colrmatch) continue;

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

			countrec = (eventcount_t *)bbh_item(eventhost, BBH_DATA);
			while (countrec && (countrec->service != eventcolumn)) countrec = countrec->next;
			if (countrec == NULL) {
				countrec = (eventcount_t *)calloc(1, sizeof(eventcount_t));
				countrec->service = eventcolumn;
				countrec->next = (eventcount_t *)bbh_item(eventhost, BBH_DATA);
				bbh_set_item(eventhost, BBH_DATA, (void *)countrec);
			}
			countrec->count++;
		}
	}

	/* Count the state changes per host */
	svccounthead = hostcounthead = NULL;
	for (hostwalk = first_host(); (hostwalk); hostwalk = next_host(hostwalk)) {
		eventcount_t *swalk;
		countlist_t *hrec, *srec;

		swalk = (eventcount_t *)bbh_item(hostwalk, BBH_DATA); if (!swalk) continue;

		hrec = (countlist_t *)malloc(sizeof(countlist_t));
		hrec->src = hostwalk;
		hrec->total = 0;
		hrec->next = hostcounthead;
		hostcounthead = hrec;

		for (swalk = (eventcount_t *)bbh_item(hostwalk, BBH_DATA); (swalk); swalk = swalk->next) {
			hrec->total += swalk->count;
			for (srec = svccounthead; (srec && (srec->src != (void *)swalk->service)); srec = srec->next) ;
			if (!srec) {
				srec = (countlist_t *)malloc(sizeof(countlist_t));
				srec->src = (void *)swalk->service;
				srec->total = 0;
				srec->next = svccounthead;
				svccounthead = srec;
			}
			srec->total += swalk->count;
		}
	}
	if (hostcounthead) hostcounthead = msort(hostcounthead, record_compare, record_getnext, record_setnext);
	if (svccounthead)  svccounthead = msort(svccounthead, record_compare, record_getnext, record_setnext);

	if (eventhead && (output != NULL)) {
		char *bgcolors[2] = { "#000000", "#000033" };
		int  bgcolor = 0;
		int  count;
		struct event_t *lasttoshow = eventhead;
		countlist_t *cwalk;
		unsigned long totalcount = 0;

		switch (sumtype) {
		  case S_HOST_BREAKDOWN:
			/* Request for a specific service, show breakdown by host */
			for (cwalk = hostcounthead; (cwalk); cwalk = cwalk->next) totalcount += cwalk->total;
			fprintf(output, "<table summary=\"Breakdown by host\" border=0>\n");
			fprintf(output, "<tr><th align=left>Host</th><th colspan=2>State changes</th></tr>\n");
			fprintf(output, "<tr><td colspan=3><hr width=\"100%%\"></td></tr>\n");
			for (cwalk = hostcounthead; (cwalk); cwalk = cwalk->next) {
				fprintf(output, "<tr><td align=left>%s</td><td align=right>%lu</td><td align=right>(%6.2f %%)</tr>\n",
					bbh_item(cwalk->src, BBH_HOSTNAME), 
					cwalk->total, ((100.0 * cwalk->total) / totalcount));
			}
			fprintf(output, "</table>\n");
			break;

		  case S_SERVICE_BREAKDOWN:
			/* Request for a specific host, show breakdown by service */
			for (cwalk = svccounthead; (cwalk); cwalk = cwalk->next) totalcount += cwalk->total;
			fprintf(output, "<table summary=\"Breakdown by service\" border=0>\n");
			fprintf(output, "<tr><th align=left>Service</th><th colspan=2>State changes</th></tr>\n");
			fprintf(output, "<tr><td colspan=3><hr width=\"100%%\"></td></tr>\n");
			for (cwalk = svccounthead; (cwalk); cwalk = cwalk->next) {
				fprintf(output, "<tr><td align=left>%s</td><td align=right>%lu</td><td align=right>(%6.2f %%)</tr>\n",
					((htnames_t *)cwalk->src)->name, 
					cwalk->total, ((100.0 * cwalk->total) / totalcount));
			}
			fprintf(output, "</table>\n");
			break;

		  case S_NONE:
			break;
		}


		count=0;
		walk=eventhead; 
		do {
			count++;
			lasttoshow = walk;
			walk = walk->next;
		} while (walk && (count<maxcount));

		if (maxminutes > 0)  { 
			sprintf(title, "%d events received in the past %u minutes", 
				count, (unsigned int)((getcurrenttime(NULL) - lasttoshow->eventtime) / 60));
		}
		else {
			sprintf(title, "%d events received.", count);
		}

		fprintf(output, "<BR><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"$EVENTSTITLE\" BORDER=0>\n");
		fprintf(output, "<TR BGCOLOR=\"#333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"#33ebf4\">%s</FONT></TD></TR>\n", title);

		for (walk=eventhead; (walk != lasttoshow->next); walk=walk->next) {
			char *hostname = bbh_item(walk->host, BBH_HOSTNAME);

			fprintf(output, "<TR BGCOLOR=%s>\n", bgcolors[bgcolor]);
			bgcolor = ((bgcolor + 1) % 2);

			fprintf(output, "<TD ALIGN=CENTER>%s</TD>\n", ctime(&walk->eventtime));

			if (walk->newcolor == COL_CLEAR) {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=black><FONT COLOR=white>%s</FONT></TD>\n",
					hostname);
			}
			else {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n",
					colorname(walk->newcolor), hostname);
			}

			fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", walk->service->name);
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(hostname, walk->service->name, walk->changetime, NULL));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=\"%s\" TITLE=\"%s\"></A>\n", 
				xgetenv("BBSKIN"), dotgiffilename(walk->oldcolor, 0, 0), 
				xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"), 
				colorname(walk->oldcolor), colorname(walk->oldcolor));
			fprintf(output, "<IMG SRC=\"%s/arrow.gif\" BORDER=0 ALT=\"From -&gt; To\">\n", 
				xgetenv("BBSKIN"));
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(hostname, walk->service->name, walk->eventtime, NULL));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=\"%s\" TITLE=\"%s\"></A></TD>\n", 
				xgetenv("BBSKIN"), dotgiffilename(walk->newcolor, 0, 0), 
				xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"), 
				colorname(walk->newcolor), colorname(walk->newcolor));
			fprintf(output, "</TR>\n");
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
	else if (output != NULL) {
		/* No events during the past maxminutes */
		if (eventlog)
			sprintf(title, "No events received in the last %d minutes", maxminutes);
		else
			strcpy(title, "No events logged");

		fprintf(output, "<CENTER><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"#333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"#33ebf4\">%s</FONT></TD>\n", title);
		fprintf(output, "</TR>\n");
		fprintf(output, "</TABLE>\n");
		fprintf(output, "</CENTER>\n");
	}

	if (eventlog) fclose(eventlog);

	if (pageregexp) pcre_free(pageregexp);
	if (hostregexp) pcre_free(hostregexp);
	if (testregexp) pcre_free(testregexp);
	if (colrregexp) pcre_free(colrregexp);

	if (eventlist) *eventlist = eventhead;
	if (hostcounts) *hostcounts = hostcounthead;
	if (servicecounts) *servicecounts = svccounthead;
}

