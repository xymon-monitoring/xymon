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

static char rcsid[] = "$Id: debug.c,v 1.13 2003-03-03 12:41:08 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>

#include "bbgen.h"
#include "debug.h"

int debug = 0;
int timing = 0;

typedef struct {
	char		*eventtext;
	struct timeval 	eventtime;
} timestamp_t;

#define MAX_DBGTIMES 100
static timestamp_t dbgtimes[MAX_DBGTIMES];
static int         dbgtimecount = 0;

#ifdef DEBUG
void dprintf(const char *fmt, ...)
{
	va_list args;

	if (debug) {
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
		fflush(stdout);
	}
}
#endif

void add_timestamp(const char *msg)
{
	struct timezone tz;

	if (timing && (dbgtimecount < MAX_DBGTIMES)) {
		gettimeofday(&dbgtimes[dbgtimecount].eventtime, &tz);
		dbgtimes[dbgtimecount].eventtext = malloc(strlen(msg)+1);
		strcpy(dbgtimes[dbgtimecount].eventtext,msg);
		dbgtimecount++;
	}
}

void show_timestamps(void)
{
	int i;
	long difsec, difusec;

	if (!timing) return;

	printf("\n\nTIME SPENT\n");
	printf("Event                                   ");
	printf("         Starttime");
	printf("          Duration\n");

	for (i=0; (i<dbgtimecount); i++) {
		printf("%-40s ", dbgtimes[i].eventtext);
		printf("%10lu.%06lu ", dbgtimes[i].eventtime.tv_sec, dbgtimes[i].eventtime.tv_usec);
		if (i>0) {
			difsec  = dbgtimes[i].eventtime.tv_sec - dbgtimes[i-1].eventtime.tv_sec;
			difusec = dbgtimes[i].eventtime.tv_usec - dbgtimes[i-1].eventtime.tv_usec;
			if (difusec < 0) {
				difsec -= 1;
				difusec += 1000000;
			}
			printf("%10lu.%06lu ", difsec, difusec);
		}
		else printf("                -");
		printf("\n");
	}
}


const char *textornull(const char *text)
{
	return (text ? text : "(NULL)");
}

void dumplinks(link_t *head)
{
#ifdef DEBUG
	link_t *l;

	for (l = head; l; l = l->next) {
		printf("Link for host %s, URL/filename %s/%s\n", l->name, l->urlprefix, l->filename);
	}
#endif
}


void dumphosts(host_t *head, char *prefix)
{
#ifdef DEBUG
	host_t *h;
	entry_t *e;
	char	format[512];

	strcpy(format, prefix);
	strcat(format, "Host: %s, ip: %s, color: %d, old: %d, noprop-y: %s, noprop-r: %s, link: %s\n");

	for (h = head; (h); h = h->next) {
		printf(format, h->hostname, h->ip, h->color, h->oldage,
			(h->nopropyellowtests ? h->nopropyellowtests : ""), 
			(h->nopropredtests ? h->nopropredtests : ""), 
			h->link->filename);
		for (e = h->entries; (e); e = e->next) {
			printf("\t\t\t\t\tTest: %s, alert %d, propagate %d, state %d, age: %s, oldage: %d\n", 
				e->column->name, e->alert, e->propagate, e->color, e->age, e->oldage);
		}
	}
#endif
}

void dumpgroups(group_t *head, char *prefix, char *hostprefix)
{
#ifdef DEBUG
	group_t *g;
	char    format[512];

	strcpy(format, prefix);
	strcat(format, "Group: %s\n");

	for (g = head; (g); g = g->next) {
		printf(format, g->title);
		dumphosts(g->hosts, hostprefix);
	}
#endif
}

void dumphostlist(hostlist_t *head)
{
#ifdef DEBUG
	hostlist_t *h;

	for (h=head; (h); h=h->next) {
		printf("Hostlist entry: Hostname %s\n", h->hostentry->hostname);
	}
#endif
}


void dumpstatelist(state_t *head)
{
#ifdef DEBUG
	state_t *s;

	for (s=head; (s); s=s->next) {
		printf("test:%s, state: %d, alert: %d, propagate: %d, oldage: %d, age: %s\n",
			s->entry->column->name,
			s->entry->color,
			s->entry->alert,
			s->entry->propagate,
			s->entry->oldage,
			s->entry->age);
	}
#endif
}

void dumpall(bbgen_page_t *head)
{
#ifdef DEBUG
	bbgen_page_t *p, *q;

	for (p=head; p; p = p->next) {
		printf("%sPage: %s, color: %d, oldage: %d, title=%s\n", 
                       (strlen(p->name) == 0) ? "" : "    ", p->name, p->color, p->oldage, p->title);
		for (q = p->subpages; (q); q = q->next) {
			printf("\tSubpage: %s, color=%d, oldage=%d, title=%s\n", 
				q->name, q->color, q->oldage, q->title);
			dumpgroups(q->groups, "\t\t", "\t\t    ");
			dumphosts(q->hosts, "\t    ");
		}

		dumpgroups(p->groups, "\t","\t    ");
		dumphosts(p->hosts, "    ");
	}
	dumphosts(head->hosts, "");
#endif
}


