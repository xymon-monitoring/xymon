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

static char rcsid[] = "$Id: process.c,v 1.14 2003-06-17 08:25:08 henrik Exp $";

#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

#include "bbgen.h"
#include "process.h"
#include "util.h"
#include "debug.h"

static int wantedcolumn(char *current, char *wanted)
{
	char *tag;
	int result;

	tag = malloc(strlen(current)+3);
	sprintf(tag, "|%s|", current);
	result = (strstr(wanted, tag) != NULL);

	free(tag);
	return result;
}


void calc_hostcolors(hostlist_t *head)
{
	int		color, oldage;
	hostlist_t 	*h, *cwalk;
	entry_t		*e;

	for (h = head; (h); h = h->next) {
		color = 0; oldage = 1;

		for (e = h->hostentry->entries; (e); e = e->next) {
			if (e->propagate && (e->color > color)) color = e->color;
			oldage &= e->oldage;
		}

		/* Blue and clear is not propagated upwards */
		if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;

		h->hostentry->color = color;
		h->hostentry->oldage = oldage;

		/* Need to update the clones also */
		for (cwalk = h->clones; (cwalk); cwalk = cwalk->next) {
			cwalk->hostentry->color = color;
			cwalk->hostentry->oldage = oldage;
		}
	}
}


void calc_pagecolors(bbgen_page_t *phead)
{
	bbgen_page_t 	*p, *toppage;
	group_t *g;
	host_t  *h;
	int	color, oldage;

	for (toppage=phead; (toppage); toppage = toppage->next) {

		/* Start with the color of immediate hosts */
		color = -1; oldage = 1;
		for (h = toppage->hosts; (h); h = h->next) {
			if (h->color > color) color = h->color;
			oldage &= h->oldage;
		}

		/* Then adjust with the color of hosts in immediate groups */
		for (g = toppage->groups; (g); g = g->next) {
			for (h = g->hosts; (h); h = h->next) {
				if (g->onlycols == NULL) {
					/* No group-only directive - use host color */
					if (h->color > color) color = h->color;
					oldage &= h->oldage;
				}
				else {
					/* This is a group-only directive. Color must be
					 * based on the tests included in the group-only
					 * directive, NOT all tests present for the host.
					 * So we need to re-calculate host color from only
					 * the selected tests.
					 */
					entry_t *e;

					for (e = h->entries; (e); e = e->next) {
						if ( e->propagate && 
						     (e->color > color) &&
						     wantedcolumn(e->column->name, g->onlycols) )
							color = e->color;
							oldage &= e->oldage;
					}

					/* Blue and clear is not propagated upwards */
					if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;
				}
			}
		}

		/* Then adjust with the color of subpages, if any.  */
		/* These must be calculated first!                  */
		if (toppage->subpages) {
			calc_pagecolors(toppage->subpages);
		}

		for (p = toppage->subpages; (p); p = p->next) {
			if (p->color > color) color = p->color;
			oldage &= p->oldage;
		}

		if (color == -1) {
			/*
			 * If no hosts or subpages, all goes green.
			 */
			color = COL_GREEN;
			oldage = 1;
		}

		toppage->color = color;
		toppage->oldage = oldage;
	}
}


void delete_old_acks(void)
{
	DIR             *bbacks;
	struct dirent   *d;
	struct stat     st;
	time_t		now = time(NULL);
	char		fn[MAX_PATH];

	bbacks = opendir(getenv("BBACKS"));
	if (!bbacks) {
		errprintf("No BBACKS! Cannot cd to directory %s\n", getenv("BBACKS"));
		return;
        }

	chdir(getenv("BBACKS"));
	while ((d = readdir(bbacks))) {
		strcpy(fn, d->d_name);
		if (strncmp(fn, "ack.", 4) == 0) {
			stat(fn, &st);
			if (S_ISREG(st.st_mode) && (st.st_mtime < now)) {
				unlink(fn);
			}
		}
	}
	closedir(bbacks);
}

void send_summaries(summary_t *sumhead)
{
	summary_t *s;
	time_t now = time(NULL);
	pid_t childpid;
	char *bbcmd;

	bbcmd = getenv("BB");
	if (!bbcmd) {
		errprintf("BB not defined!");
		return;
	}

	for (s = sumhead; (s); s = s->next) {
		char *suburl;
		int summarycolor = -1;
		char summsg[MAXMSG];

		/* Decide which page to pick the color from for this summary. */
		suburl = s->url;
		if (strncmp(suburl, "http://", 7) == 0) {
			char *p;

			/* Skip hostname part */
			suburl += 7;			/* Skip "http://" */
			p = strchr(suburl, '/');	/* Find next '/' */
			if (p) suburl = p;
		}
		if (strncmp(suburl, getenv("BBWEB"), strlen(getenv("BBWEB"))) == 0) 
			suburl += strlen(getenv("BBWEB"));
		if (*suburl == '/') suburl++;

		if (debug) printf("summ1: s->url=%s, suburl=%s\n", s->url, suburl);

		if      (strcmp(suburl, "bb.html") == 0) summarycolor = bb_color;
		else if (strcmp(suburl, "index.html") == 0) summarycolor = bb_color;
		else if (strcmp(suburl, "") == 0) summarycolor = bb_color;
		else if (strcmp(suburl, "bb2.html") == 0) summarycolor = bb2_color;
		else if (strcmp(suburl, "bbnk.html") == 0) summarycolor = bbnk_color;
		else {
			/* 
			 * Specific page - find it in the page tree.
			 */
			char *p, *pg;
			bbgen_page_t *pgwalk;
			bbgen_page_t *sourcepg = NULL;
			char *urlcopy = malcop(suburl);

			/*
			 * Walk the page tree
			 */
			pg = urlcopy; sourcepg = pagehead;
			do {
				p = strchr(pg, '/');
				if (p) *p = '\0';

				dprintf("Searching for page %s\n", pg);
				for (pgwalk = sourcepg->subpages; (pgwalk && (strcmp(pgwalk->name, pg) != 0)); pgwalk = pgwalk->next);
				if (pgwalk != NULL) {
					sourcepg = pgwalk;

					if (p) { 
						*p = '/'; pg = p+1; 
					}
					else pg = NULL;
				}
				else pg = NULL;
			} while (pg);

			dprintf("Summary search for %s found page %s (title:%s), color %d\n",
				suburl, sourcepg->name, sourcepg->title, sourcepg->color);
			summarycolor = sourcepg->color;
			free(urlcopy);
		}

		if (summarycolor == -1) {
			errprintf("Could not determine sourcepage for summary %s\n", s->url);
			summarycolor = pagehead->color;
		}

		/* Send the summary message */
		sprintf(summsg, "summary summary.%s %s %s %s",
			s->name, colorname(summarycolor), s->url, ctime(&now));

		childpid = fork();
		if (childpid == -1) {
			errprintf("Fork error while trying to send summary\n");
		}
		else if (childpid == 0) {
			execl(bbcmd, "bb: bbd summary", s->receiver, summsg, NULL);
		}
		else {
			wait(NULL);
		}
	}
}

