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

static char rcsid[] = "$Id: debug.c,v 1.19 2003-05-21 22:23:36 henrik Exp $";

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

void show_timestamps(char *buffer)
{
	int i;
	long difsec, difusec;
	char *outbuf;
	char buf1[80];

	if (!timing) return;

	outbuf = ((buffer == NULL) ? malloc(80*(dbgtimecount+10)) : buffer);

	strcpy(outbuf, "\n\nTIME SPENT\n");
	strcat(outbuf, "Event                                   ");
	strcat(outbuf, "         Starttime");
	strcat(outbuf, "          Duration\n");

	for (i=0; (i<dbgtimecount); i++) {
		sprintf(buf1, "%-40s ", dbgtimes[i].eventtext);
		strcat(outbuf, buf1);
		sprintf(buf1, "%10lu.%06lu ", dbgtimes[i].eventtime.tv_sec, dbgtimes[i].eventtime.tv_usec);
		strcat(outbuf, buf1);
		if (i>0) {
			difsec  = dbgtimes[i].eventtime.tv_sec - dbgtimes[i-1].eventtime.tv_sec;
			difusec = dbgtimes[i].eventtime.tv_usec - dbgtimes[i-1].eventtime.tv_usec;
			if (difusec < 0) {
				difsec -= 1;
				difusec += 1000000;
			}
			sprintf(buf1, "%10lu.%06lu ", difsec, difusec);
			strcat(outbuf, buf1);
		}
		else strcat(outbuf, "                -");
		strcat(outbuf, "\n");
	}

	difsec  = dbgtimes[dbgtimecount-1].eventtime.tv_sec - dbgtimes[0].eventtime.tv_sec;
	difusec = dbgtimes[dbgtimecount-1].eventtime.tv_usec - dbgtimes[0].eventtime.tv_usec;
	if (difusec < 0) {
		difsec -= 1;
		difusec += 1000000;
	}
	sprintf(buf1, "%-40s ", "TIME TOTAL"); strcat(outbuf, buf1);
	sprintf(buf1, "%-18s", ""); strcat(outbuf, buf1);
	sprintf(buf1, "%10lu.%06lu ", difsec, difusec); strcat(outbuf, buf1);
	strcat(outbuf, "\n");

	if (buffer == NULL) {
		printf("%s", outbuf);
		free(outbuf);
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
	strcat(format, "Host: %s, ip: %s, color: %d, old: %d, pretitle: '%s', noprop-y: %s, noprop-r: %s, link: %s, graphs: %s\n");

	for (h = head; (h); h = h->next) {
		printf(format, h->hostname, h->ip, h->color, h->oldage,
			textornull(h->pretitle),
			textornull(h->nopropyellowtests), 
			textornull(h->nopropredtests), 
			h->link->filename,
			textornull(h->larrdgraphs));
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
	strcat(format, "Group: %s, pretitle: '%s'\n");

	for (g = head; (g); g = g->next) {
		printf(format, textornull(g->title), textornull(g->pretitle));
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

void dumponepagewithsubs(bbgen_page_t *curpage, char *indent)
{
#ifdef DEBUG
	bbgen_page_t *levelpage;

	char newindent[100];
	char newindentextra[105];

	strcpy(newindent, indent);
	strcat(newindent, "\t");
	strcpy(newindentextra, newindent);
	strcat(newindentextra, "    ");

	for (levelpage = curpage; (levelpage); levelpage = levelpage->next) {
		printf("%sPage: %s, color=%d, oldage=%d, title=%s, pretitle=%s\n", 
			indent, levelpage->name, levelpage->color, levelpage->oldage, textornull(levelpage->title), textornull(levelpage->pretitle));

		dumpgroups(levelpage->groups, newindent, newindentextra);
		dumphosts(levelpage->hosts, newindentextra);
		dumponepagewithsubs(levelpage->subpages, newindent);
	}
#endif
}

void dumpall(bbgen_page_t *head)
{
#ifdef DEBUG
	dumponepagewithsubs(head, "");
#endif
}


