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

static char rcsid[] = "$Id: debug.c,v 1.29 2004-08-04 11:31:38 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"

int debug = 0;
int timing = 0;

typedef struct timestamp_t {
	char		*eventtext;
	struct timeval 	eventtime;
	struct timestamp_t *prev;
	struct timestamp_t *next;
} timestamp_t;

static timestamp_t *stamphead = NULL;
static timestamp_t *stamptail = NULL;

void dprintf(const char *fmt, ...)
{
	va_list args;

	if (debug) {
		char timestr[30];
		time_t now = time(NULL);

		strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S",
			 localtime(&now));
		printf("%s ", timestr);

		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
		fflush(stdout);
	}
}

void add_timestamp(const char *msg)
{
	struct timezone tz;

	if (timing) {
		timestamp_t *newstamp = (timestamp_t *) malloc(sizeof(timestamp_t));

		gettimeofday(&newstamp->eventtime, &tz);
		newstamp->eventtext = malcop(msg);

		if (stamphead == NULL) {
			newstamp->next = newstamp->prev = NULL;
			stamphead = newstamp;
		}
		else {
			newstamp->prev = stamptail;
			newstamp->next = NULL;
			stamptail->next = newstamp;
		}
		stamptail = newstamp;
	}
}

void show_timestamps(char **buffer)
{
	timestamp_t *s;
	long difsec, difusec;
	char *outbuf = (char *) malloc(4096);
	int outbuflen = 4096;
	char buf1[80];

	if (!timing || (stamphead == NULL)) return;

	strcpy(outbuf, "\n\nTIME SPENT\n");
	strcat(outbuf, "Event                                   ");
	strcat(outbuf, "         Starttime");
	strcat(outbuf, "          Duration\n");

	for (s=stamphead; (s); s=s->next) {
		sprintf(buf1, "%-40s ", s->eventtext);
		strcat(outbuf, buf1);
		sprintf(buf1, "%10lu.%06lu ", s->eventtime.tv_sec, s->eventtime.tv_usec);
		strcat(outbuf, buf1);
		if (s->prev) {
			difsec  = s->eventtime.tv_sec - ((timestamp_t *)s->prev)->eventtime.tv_sec;
			difusec = s->eventtime.tv_usec - ((timestamp_t *)s->prev)->eventtime.tv_usec;
			if (difusec < 0) {
				difsec -= 1;
				difusec += 1000000;
			}
			sprintf(buf1, "%10lu.%06lu ", difsec, difusec);
			strcat(outbuf, buf1);
		}
		else strcat(outbuf, "                -");
		strcat(outbuf, "\n");

		if ((outbuflen - strlen(outbuf)) < 200) {
			outbuflen += 4096;
			outbuf = (char *) realloc(outbuf, outbuflen);
		}
	}

	difsec  = stamptail->eventtime.tv_sec - stamphead->eventtime.tv_sec;
	difusec = stamptail->eventtime.tv_usec - stamphead->eventtime.tv_usec;
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
	else *buffer = outbuf;
}


long total_runtime(void)
{
	if (timing)
		return (stamptail->eventtime.tv_sec - stamphead->eventtime.tv_sec);
	else
		return 0;
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
	strcat(format, "Host: %s, ip: %s, name: %s, color: %d, old: %d, anywaps: %d, wapcolor: %d, pretitle: '%s', noprop-y: %s, noprop-r: %s, noprop-p: %s, noprop-ack: %s, link: %s, graphs: %s, waps: %s\n");

	for (h = head; (h); h = h->next) {
		printf(format, h->hostname, h->ip, textornull(h->displayname), h->color, h->oldage,
			h->anywaps, h->wapcolor,
			textornull(h->pretitle),
			textornull(h->nopropyellowtests), 
			textornull(h->nopropredtests), 
			textornull(h->noproppurpletests), 
			textornull(h->nopropacktests), 
			h->link->filename,
			textornull(h->larrdgraphs), textornull(h->waps));
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


