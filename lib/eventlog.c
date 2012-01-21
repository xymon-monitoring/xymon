/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This displays the "eventlog" found on the "All non-green status" page.     */
/* It also implements a CGI tool to show an eventlog for a given period of    */
/* time, as a reporting function.                                             */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/* Host/test/color/start/end filtering code by Eric Schwimmer 2005            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: eventlog.c 6712 2011-07-31 21:01:52Z storner $";

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

#include "libxymon.h"

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

static char *string_time(time_t timestamp)
{
	static char result[20];

	strftime(result, sizeof(result), "%Y/%m/%d@%H:%M:%S", localtime(&timestamp));
	return result;
}

int record_compare(void **a, void **b)
{
	countlist_t **reca = (countlist_t **)a, **recb = (countlist_t **)b;

	/* Sort the countlist_t records in reverse */
	if ( (*reca)->total > (*recb)->total )  return -1;
	else if ( (*reca)->total < (*recb)->total ) return 1;
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

static void count_events(countlist_t **hostcounthead, countlist_t **svccounthead)
{
	void *hostwalk;

	for (hostwalk = first_host(); (hostwalk); hostwalk = next_host(hostwalk, 0)) {
		eventcount_t *swalk;
		countlist_t *hrec, *srec;

		swalk = (eventcount_t *)xmh_item(hostwalk, XMH_DATA); if (!swalk) continue;

		hrec = (countlist_t *)malloc(sizeof(countlist_t));
		hrec->src = hostwalk;
		hrec->total = 0;
		hrec->next = *hostcounthead;
		*hostcounthead = hrec;

		for (swalk = (eventcount_t *)xmh_item(hostwalk, XMH_DATA); (swalk); swalk = swalk->next) {
			hrec->total += swalk->count;
			for (srec = *svccounthead; (srec && (srec->src != (void *)swalk->service)); srec = srec->next) ;
			if (!srec) {
				srec = (countlist_t *)malloc(sizeof(countlist_t));
				srec->src = (void *)swalk->service;
				srec->total = 0;
				srec->next = *svccounthead;
				*svccounthead = srec;
			}
			srec->total += swalk->count;
		}
	}
}

typedef struct ed_t {
	event_t *event;
	struct ed_t *next;
} ed_t;
typedef struct elist_t {
	htnames_t *svc;
	ed_t *head, *tail;
	struct elist_t *next;
} elist_t;

static void dump_eventtree(void)
{
	void *hwalk;
	elist_t *lwalk;
	ed_t *ewalk;

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		printf("%s\n", xmh_item(hwalk, XMH_HOSTNAME));
		lwalk = (elist_t *)xmh_item(hwalk, XMH_DATA);
		while (lwalk) {
			printf("\t%s\n", lwalk->svc->name);
			ewalk = lwalk->head;
			while (ewalk) {
				printf("\t\t%ld->%ld = %6ld %s\n",
					(long) ewalk->event->changetime,
					(long) ewalk->event->eventtime,
					(long) ewalk->event->duration,
					colorname(ewalk->event->oldcolor));
				ewalk = ewalk->next;
			}
			lwalk = lwalk->next;
		}
	}
}

void dump_countlists(countlist_t *hosthead, countlist_t *svchead)
{
	countlist_t *cwalk;

	printf("Hosts\n");
	for (cwalk = hosthead; (cwalk); cwalk = cwalk->next) {
		printf("\t%20s : %lu\n", xmh_item(cwalk->src, XMH_HOSTNAME), cwalk->total);
	}
	printf("\n");

	printf("Services\n");
	for (cwalk = svchead; (cwalk); cwalk = cwalk->next) {
		printf("\t%20s : %lu\n", ((htnames_t *)cwalk->src)->name, cwalk->total);
	}
	printf("\n");
}

static int  eventfilter(void *hinfo, char *testname,
			pcre *pageregexp, pcre *expageregexp,
			pcre *hostregexp, pcre *exhostregexp,
			pcre *testregexp, pcre *extestregexp,
			int ignoredialups, f_hostcheck hostcheck)
{
	int pagematch, hostmatch, testmatch;
	char *hostname = xmh_item(hinfo, XMH_HOSTNAME);
	int ovector[30];

	if (ignoredialups && xmh_item(hinfo, XMH_FLAG_DIALUP)) return 0;
	if (hostcheck && (hostcheck(hostname) == 0)) return 0;

	if (pageregexp) {
		char *pagename;

		pagename = xmh_item_multi(hinfo, XMH_PAGEPATH);
		pagematch = 0;
		while (!pagematch && pagename) {
			pagematch = (pcre_exec(pageregexp, NULL, pagename, strlen(pagename), 0, 0, 
					ovector, (sizeof(ovector)/sizeof(int))) >= 0);
			pagename = xmh_item_multi(NULL, XMH_PAGEPATH);
		}
	}
	else
		pagematch = 1;
	if (!pagematch) return 0;

	if (expageregexp) {
		char *pagename;

		pagename = xmh_item_multi(hinfo, XMH_PAGEPATH);
		pagematch = 0;
		while (!pagematch && pagename) {
			pagematch = (pcre_exec(expageregexp, NULL, pagename, strlen(pagename), 0, 0, 
					ovector, (sizeof(ovector)/sizeof(int))) >= 0);
			pagename = xmh_item_multi(NULL, XMH_PAGEPATH);
		}
	}
	else
		pagematch = 0;
	if (pagematch) return 0;

	if (hostregexp)
		hostmatch = (pcre_exec(hostregexp, NULL, hostname, strlen(hostname), 0, 0, 
				ovector, (sizeof(ovector)/sizeof(int))) >= 0);
	else
		hostmatch = 1;
	if (!hostmatch) return 0;

	if (exhostregexp)
		hostmatch = (pcre_exec(exhostregexp, NULL, hostname, strlen(hostname), 0, 0, 
				ovector, (sizeof(ovector)/sizeof(int))) >= 0);
	else
		hostmatch = 0;
	if (hostmatch) return 0;

	if (testregexp)
		testmatch = (pcre_exec(testregexp, NULL, testname, strlen(testname), 0, 0, 
				ovector, (sizeof(ovector)/sizeof(int))) >= 0);
	else
		testmatch = 1;
	if (!testmatch) return 0;

	if (extestregexp)
		testmatch = (pcre_exec(extestregexp, NULL, testname, strlen(testname), 0, 0, 
				ovector, (sizeof(ovector)/sizeof(int))) >= 0);
	else
		testmatch = 0;
	if (testmatch) return 0;

	return 1;
}


static void count_duration(time_t fromtime, time_t totime,
			   pcre *pageregexp, pcre *expageregexp,
			   pcre *hostregexp, pcre *exhostregexp,
			   pcre *testregexp, pcre *extestregexp,
			   int ignoredialups, f_hostcheck hostcheck,
			   event_t *eventhead, countlist_t **hostcounthead, countlist_t **svccounthead)
{
	void *hwalk;
	elist_t *lwalk;
	event_t *ewalk;
	ed_t *ed;
	sendreturn_t *bdata;

	/*
	 * Restructure the event-list so we have a tree instead:
	 *
	 *      HostRecord
	 *      |  *Data ---->  EventList
	 *      |               |  *Service
	 *      |               |  *EventHead --> Event --> Event --> Event
	 *      |               |  *EventTail --------------------------^
	 *      |               |
	 *      |               v
	 *      |
	 *      v
	 *
	 */
	for (ewalk = eventhead; (ewalk); ewalk = ewalk->next) {
		lwalk = (elist_t *)xmh_item(ewalk->host, XMH_DATA);
		while (lwalk && (lwalk->svc != ewalk->service)) lwalk = lwalk->next;
		if (lwalk == NULL) {
			lwalk = (elist_t *)calloc(1, sizeof(elist_t));
			lwalk->svc = ewalk->service;
			lwalk->next = (elist_t *)xmh_item(ewalk->host, XMH_DATA);
			xmh_set_item(ewalk->host, XMH_DATA, (void *)lwalk);
		}

		ed = (ed_t *)calloc(1, sizeof(ed_t));
		ed->event = ewalk;
		ed->next = lwalk->head;
		if (lwalk->head == NULL) lwalk->tail = ed;
		lwalk->head = ed;
	}

	if (debug) {
		printf("\n\nEventtree before fixups\n\n");
		dump_eventtree();
	}
	
	/* 
	 * Next, we must add a pseudo record for the current state.
	 * This is for those statuses that haven't changed since the 
	 * start of our data-collection period - they won't have any events
	 * so we cannot tell what color they are. By grabbing the current
	 * color we can add a pseudo-event that lets us determine what the 
	 * color has been since the start of the event-period.
	 */
	bdata = newsendreturnbuf(1, NULL);
	if (sendmessage("xymondboard fields=hostname,testname,color,lastchange", NULL, XYMON_TIMEOUT, bdata) == XYMONSEND_OK) {
		char *bol, *eol;
		char *hname, *tname;
		int color;
		time_t lastchange;
		void *hrec;
		htnames_t *srec;
		char *icname = xgetenv("INFOCOLUMN");
		char *tcname = xgetenv("TRENDSCOLUMN");

		bol = getsendreturnstr(bdata, 0);
		while (bol) {
			eol = strchr(bol, '\n'); if (eol) *eol = '\0';
			hname = strtok(bol, "|");
			tname = (hname ? strtok(NULL, "|") : NULL);
			color = (tname ? parse_color(strtok(NULL, "|")) : -1);
			lastchange = ((color != -1) ? atol(strtok(NULL, "\n")) : totime+1);

			if (hname && tname && (color != -1) && (strcmp(tname, icname) != 0) && (strcmp(tname, tcname) != 0)) {
				int addrec = 1;

				hrec = hostinfo(hname);
				srec = getname(tname, 1);

				if (eventfilter(hrec, tname, 
						pageregexp, expageregexp, 
						hostregexp, exhostregexp,
						testregexp, extestregexp,
						ignoredialups, hostcheck) == 0) goto nextrecord;

				lwalk = (elist_t *)xmh_item(hrec, XMH_DATA);
				while (lwalk && (lwalk->svc != srec)) lwalk = lwalk->next;
				if (lwalk == NULL) {
					lwalk = (elist_t *)calloc(1, sizeof(elist_t));
					lwalk->svc = srec;
					lwalk->next = (elist_t *)xmh_item(hrec, XMH_DATA);
					xmh_set_item(hrec, XMH_DATA, (void *)lwalk);
				}

				/* See if we already have an event past the "totime" value */
				if (lwalk->head) {
					addrec = 0;

					ed = lwalk->head;
					while (ed && (ed->event->eventtime < totime)) ed = ed->next;

					if (ed) {
						ed->next = NULL;
						lwalk->tail = ed;
					}
					else {
						ed = (ed_t *)calloc(1, sizeof(ed_t));
						ed->event = (event_t *)calloc(1, sizeof(event_t));
						lwalk->tail->next = ed;

						ed->event->host = hrec;
						ed->event->service = srec;
						ed->event->eventtime = totime;
						ed->event->changetime = lwalk->tail->event->eventtime;
						ed->event->duration = (totime - lwalk->tail->event->eventtime);
						ed->event->newcolor = -1;
						ed->event->oldcolor = lwalk->tail->event->newcolor;
						ed->event->next = NULL;
						ed->next = NULL;

						lwalk->tail = ed;
					}
				}
				else if (lastchange < totime) {
					ed = (ed_t *)calloc(1, sizeof(ed_t));
					ed->event = (event_t *)calloc(1, sizeof(event_t));
					ed->event->host = hrec;
					ed->event->service = srec;
					ed->event->eventtime = totime;
					ed->event->changetime = (lwalk->tail ? lwalk->tail->event->eventtime : fromtime);
					ed->event->duration = (totime - ed->event->changetime);
					ed->event->newcolor = color;
					ed->event->oldcolor = (lwalk->tail ? lwalk->tail->event->newcolor : color);
					ed->event->next = NULL;
					ed->next = NULL;

					lwalk->head = lwalk->tail = ed;
				}
			}

nextrecord:
			bol = (eol ? eol+1 : NULL);
		}

		freesendreturnbuf(bdata);
	}
	else {
		errprintf("Cannot get the current state\n");
		freesendreturnbuf(bdata);
		return;
	}

	if (debug) {
		printf("\n\nEventtree after pseudo-events\n\n");
		dump_eventtree();
	}
	
	/* 
	 * Fixup the beginning-time (and duration) of the first events recorded.
	 * This is to handle events that begin BEFORE our event-logging period.
	 * Fixup the end-time (and duration) of the last events recorded.
	 * This is to handle events that end AFTER our event-logging period.
	 */
	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		elist_t *lwalk;
		event_t *erec;
		ed_t *ewalk;

		lwalk = (elist_t *)xmh_item(hwalk, XMH_DATA); 
		while (lwalk) {
			if (lwalk->head) {
				erec = lwalk->head->event;
				if (erec->changetime > totime) {
					/* First event is after our start-time. Drop the events */
					lwalk->head = lwalk->tail = NULL;
				}
				else if (erec->changetime < fromtime) {
					/* First event is before our start-time. Adjust to starttime. */
					erec->changetime = fromtime;
					erec->duration = (erec->eventtime - fromtime);
				}

				ewalk = lwalk->head;
				while (ewalk && (ewalk->event->eventtime < totime)) ewalk = ewalk->next;
				if (ewalk) {
					lwalk->tail = ewalk;
					lwalk->tail->next = 0;
				}

				if (lwalk->tail) {
					erec = lwalk->tail->event;
					if (erec->eventtime > totime) {
						/* Last event is after our end-time. Adjust to end-time */
						erec->eventtime = totime;
						erec->duration = (totime - erec->changetime);
					}
				}
			}

			lwalk = lwalk->next;
		}
	}

	if (debug) {
		printf("\n\nEventtree after fixups\n\n");
		dump_eventtree();
	}

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		countlist_t *hrec, *srec;

		hrec = (countlist_t *)malloc(sizeof(countlist_t));
		hrec->src = hwalk;
		hrec->total = 0;
		hrec->next = *hostcounthead;
		*hostcounthead = hrec;

		lwalk = (elist_t *)xmh_item(hwalk, XMH_DATA);
		while (lwalk) {
			for (srec = *svccounthead; (srec && (srec->src != (void *)lwalk->svc)); srec = srec->next) ;
			if (!srec) {
				srec = (countlist_t *)malloc(sizeof(countlist_t));
				srec->src = (void *)lwalk->svc;
				srec->total = 0;
				srec->next = *svccounthead;
				*svccounthead = srec;
			}

			if (lwalk->head) {
				ed_t *ewalk = lwalk->head;

				while (ewalk) {
					if (ewalk->event->oldcolor >= COL_YELLOW) {
						hrec->total += ewalk->event->duration;
						srec->total += ewalk->event->duration;
					}
					ewalk = ewalk->next;
				}
			}

			lwalk = lwalk->next;
		}
	}

	if (debug) dump_countlists(*hostcounthead, *svccounthead);
}

void do_eventlog(FILE *output, int maxcount, int maxminutes, char *fromtime, char *totime, 
		char *pageregex, char *expageregex,
		char *hostregex, char *exhostregex,
		char *testregex, char *extestregex,
		char *colrregex, int ignoredialups,
		f_hostcheck hostcheck,
		event_t **eventlist, countlist_t **hostcounts, countlist_t **servicecounts,
		countsummary_t counttype, eventsummary_t sumtype, char *periodstring)
{
	FILE *eventlog;
	char eventlogfilename[PATH_MAX];
	time_t firstevent = 0;
	time_t lastevent = getcurrenttime(NULL);
	event_t	*eventhead = NULL;
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
	countlist_t *hostcounthead = NULL, *svccounthead = NULL;

	if (eventlist) *eventlist = NULL;
	if (hostcounts) *hostcounts = NULL;
	if (servicecounts) *servicecounts = NULL;

	havedoneeventlog = 1;

	if ((maxminutes > 0) && (fromtime || totime)) {
		fprintf(output, "<B>Only one time interval type is allowed!</B>");
		return;
	}

	if (fromtime) {
		firstevent = eventreport_time(fromtime);
		if(firstevent < 0) {
			if (output) fprintf(output,"<B>Invalid 'from' time: %s</B>", htmlquoted(fromtime));
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
		lastevent = eventreport_time(totime);
		if (lastevent < 0) {
			if (output) fprintf(output,"<B>Invalid 'to' time: %s</B>", htmlquoted(totime));
			return;
		}
		if (lastevent < firstevent) {
			if (output) fprintf(output,"<B>'to' time must be after 'from' time.</B>");
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

	sprintf(eventlogfilename, "%s/allevents", xgetenv("XYMONHISTDIR"));
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
					curtime = 0;
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
		/* For DURATION counts, we must parse all events until now */
		if ((counttype != XYMON_COUNT_DURATION) && (eventtime > lastevent)) break;
		eventhost = hostinfo(hostname);
		eventcolumn = getname(svcname, 1);

		if ( (itemsfound == 8) && 
		     (eventtime >= firstevent) && 
		     (eventhost && !xmh_item(eventhost, XMH_FLAG_NONONGREEN)) && 
		     (wanted_eventcolumn(svcname)) ) {

			if (eventfilter(eventhost, svcname, 
					pageregexp, expageregexp, 
					hostregexp, exhostregexp,
					testregexp, extestregexp,
					ignoredialups, hostcheck) == 0) continue;

			/* For duration counts, record all events. We'll filter out the colors later. */
			if (colrregexp && (counttype != XYMON_COUNT_DURATION)) {
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

			if (counttype != XYMON_COUNT_DURATION) {
				countrec = (eventcount_t *)xmh_item(eventhost, XMH_DATA);
				while (countrec && (countrec->service != eventcolumn)) countrec = countrec->next;
				if (countrec == NULL) {
					countrec = (eventcount_t *)calloc(1, sizeof(eventcount_t));
					countrec->service = eventcolumn;
					countrec->next = (eventcount_t *)xmh_item(eventhost, XMH_DATA);
					xmh_set_item(eventhost, XMH_DATA, (void *)countrec);
				}
				countrec->count++;
			}
		}
	}

	/* Count the state changes per host */
	svccounthead = hostcounthead = NULL;
	switch (counttype) {
	  case XYMON_COUNT_EVENTS: count_events(&hostcounthead, &svccounthead); break;
	  case XYMON_COUNT_DURATION: count_duration(firstevent, lastevent,
					       pageregexp, expageregexp,
					       hostregexp, exhostregexp,
					       testregexp, extestregexp,
					       ignoredialups, hostcheck,
					       eventhead, &hostcounthead, &svccounthead); break;
	  default: break;
	}

	if (hostcounthead) hostcounthead = msort(hostcounthead, record_compare, record_getnext, record_setnext);
	if (svccounthead)  svccounthead = msort(svccounthead, record_compare, record_getnext, record_setnext);

	if (eventhead && (output != NULL)) {
		char *bgcolors[2] = { "#000000", "#000033" };
		int  bgcolor = 0;
		struct event_t *ewalk, *lasttoshow = eventhead;
		countlist_t *cwalk;
		unsigned long totalcount = 0;

		if (periodstring) fprintf(output, "<p><font size=+1>%s</font></p>\n", htmlquoted(periodstring));

		switch (sumtype) {
		  case XYMON_S_HOST_BREAKDOWN:
			/* Request for a specific service, show breakdown by host */
			for (cwalk = hostcounthead; (cwalk); cwalk = cwalk->next) totalcount += cwalk->total;
			fprintf(output, "<table summary=\"Breakdown by host\" border=0>\n");
			fprintf(output, "<tr><th align=left>Host</th><th colspan=2>%s</th></tr>\n",
				(counttype == XYMON_COUNT_EVENTS) ? "State changes" : "Seconds red/yellow");
			fprintf(output, "<tr><td colspan=3><hr width=\"100%%\"></td></tr>\n");
			for (cwalk = hostcounthead; (cwalk && (cwalk->total > 0)); cwalk = cwalk->next) {
				fprintf(output, "<tr><td align=left>%s</td><td align=right>%lu</td><td align=right>(%6.2f %%)</tr>\n",
					xmh_item(cwalk->src, XMH_HOSTNAME), 
					cwalk->total, ((100.0 * cwalk->total) / totalcount));
			}
			fprintf(output, "</table>\n");
			break;

		  case XYMON_S_SERVICE_BREAKDOWN:
			/* Request for a specific host, show breakdown by service */
			for (cwalk = svccounthead; (cwalk); cwalk = cwalk->next) totalcount += cwalk->total;
			fprintf(output, "<table summary=\"Breakdown by service\" border=0>\n");
			fprintf(output, "<tr><th align=left>Service</th><th colspan=2>%s</th></tr>\n",
				(counttype == XYMON_COUNT_EVENTS) ? "State changes" : "Seconds red/yellow");
			fprintf(output, "<tr><td colspan=3><hr width=\"100%%\"></td></tr>\n");
			for (cwalk = svccounthead; (cwalk && (cwalk->total > 0)); cwalk = cwalk->next) {
				fprintf(output, "<tr><td align=left>%s</td><td align=right>%lu</td><td align=right>(%6.2f %%)</tr>\n",
					((htnames_t *)cwalk->src)->name, 
					cwalk->total, ((100.0 * cwalk->total) / totalcount));
			}
			fprintf(output, "</table>\n");
			break;

		  case XYMON_S_NONE:
			break;
		}

		if (sumtype == XYMON_S_NONE) {
			int  count;
			count=0;
			ewalk=eventhead; 
			do {
				count++;
				lasttoshow = ewalk;
				ewalk = ewalk->next;
			} while (ewalk && (count<maxcount));
			if (ewalk) ewalk->next = NULL;	/* Terminate list if any items left */

			if (maxminutes > 0)  { 
				sprintf(title, "%d events received in the past %u minutes", 
					count, (unsigned int)((getcurrenttime(NULL) - lasttoshow->eventtime) / 60));
			}
			else {
				sprintf(title, "%d events received.", count);
			}
		}
		else {
			strcpy(title, "Events in summary");
		}

		fprintf(output, "<BR><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"$EVENTSTITLE\" BORDER=0>\n");
		fprintf(output, "<TR BGCOLOR=\"#333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"#33ebf4\">%s</FONT></TD></TR>\n", htmlquoted(title));

		for (ewalk=eventhead; (ewalk); ewalk=ewalk->next) {
			char *hostname = xmh_item(ewalk->host, XMH_HOSTNAME);

			if ( (counttype == XYMON_COUNT_DURATION) &&
			     (ewalk->oldcolor < COL_YELLOW) &&
			     (ewalk->newcolor < COL_YELLOW) ) continue;

			if ( (counttype == XYMON_COUNT_DURATION) &&
			     (ewalk->eventtime >= lastevent) ) continue;

			fprintf(output, "<TR BGCOLOR=%s>\n", bgcolors[bgcolor]);
			bgcolor = ((bgcolor + 1) % 2);

			fprintf(output, "<TD ALIGN=CENTER>%s</TD>\n", ctime(&ewalk->eventtime));

			if (ewalk->newcolor == COL_CLEAR) {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=black><FONT COLOR=white>%s</FONT></TD>\n",
					hostname);
			}
			else {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n",
					colorname(ewalk->newcolor), hostname);
			}

			fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", ewalk->service->name);
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(hostname, ewalk->service->name, ewalk->changetime, NULL));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=\"%s\" TITLE=\"%s\"></A>\n", 
				xgetenv("XYMONSKIN"), dotgiffilename(ewalk->oldcolor, 0, 0), 
				xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"), 
				colorname(ewalk->oldcolor), colorname(ewalk->oldcolor));
			fprintf(output, "<IMG SRC=\"%s/arrow.gif\" BORDER=0 ALT=\"From -&gt; To\">\n", 
				xgetenv("XYMONSKIN"));
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(hostname, ewalk->service->name, ewalk->eventtime, NULL));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=\"%s\" TITLE=\"%s\"></A></TD>\n", 
				xgetenv("XYMONSKIN"), dotgiffilename(ewalk->newcolor, 0, 0), 
				xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"), 
				colorname(ewalk->newcolor), colorname(ewalk->newcolor));
			fprintf(output, "</TR>\n");
		}

		fprintf(output, "</TABLE>\n");

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
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"#33ebf4\">%s</FONT></TD>\n", htmlquoted(title));
		fprintf(output, "</TR>\n");
		fprintf(output, "</TABLE>\n");
		fprintf(output, "</CENTER>\n");
	}

	if (eventlog) fclose(eventlog);

	if (pageregexp) pcre_free(pageregexp);
	if (hostregexp) pcre_free(hostregexp);
	if (testregexp) pcre_free(testregexp);
	if (colrregexp) pcre_free(colrregexp);

	/* Return the event- and count-lists, if wanted - or clean them up */
	if (eventlist) {
		*eventlist = eventhead;
	}
	else {
		event_t	*zombie, *ewalk = eventhead;
		while (ewalk) { zombie = ewalk; ewalk = ewalk->next; xfree(zombie); }
	}

	if (hostcounts) {
		*hostcounts = hostcounthead;
	}
	else {
		countlist_t *zombie, *hwalk = hostcounthead;
		while (hwalk) { zombie = hwalk; hwalk = hwalk->next; xfree(zombie); }

	}

	if (servicecounts) {
		*servicecounts = svccounthead;
	}
	else {
		countlist_t *zombie, *swalk = svccounthead;
		while (swalk) { zombie = swalk; swalk = swalk->next; xfree(zombie); }
	}
}

