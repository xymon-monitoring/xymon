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

static char rcsid[] = "$Id: process.c,v 1.6 2003-02-11 16:29:52 henrik Exp $";

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

void calc_hostcolors(hostlist_t *head)
{
	int		color;
	hostlist_t 	*h;
	entry_t		*e;

	for (h = head; (h); h = h->next) {
		color = 0;

		for (e = h->hostentry->entries; (e); e = e->next) {
			if (e->propagate && (e->color > color)) color = e->color;
		}

		/* Blue and clear is not propageted upwards */
		if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;

		h->hostentry->color = color;
	}
}

void calc_pagecolors(bbgen_page_t *phead)
{
	bbgen_page_t 	*p, *toppage;
	group_t *g;
	host_t  *h;
	int	color;

	for (toppage=phead; (toppage); toppage = toppage->next) {

		/* Start with the color of immediate hosts */
		color = -1;
		for (h = toppage->hosts; (h); h = h->next) {
			if (h->color > color) color = h->color;
		}

		/* Then adjust with the color of hosts in immediate groups */
		for (g = toppage->groups; (g); g = g->next) {
			for (h = g->hosts; (h); h = h->next) {
				if (h->color > color) color = h->color;
			}
		}

		/* Then adjust with the color of subpages, if any.  */
		/* These must be calculated first!                  */
		if (toppage->subpages) {
			calc_pagecolors(toppage->subpages);
		}

		for (p = toppage->subpages; (p); p = p->next) {
			if (p->color > color) color = p->color;
		}

		toppage->color = color;
	}
}


void delete_old_acks(void)
{
	DIR             *bbacks;
	struct dirent   *d;
	struct stat     st;
	time_t		now = time(NULL);
	char		fn[256];

	bbacks = opendir(getenv("BBACKS"));
	if (!bbacks) {
		perror("No BBACKS!");
		exit(1);
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
		printf("BB not defined!");
		return;
	}

	for (s = sumhead; (s); s = s->next) {
		char *suburl;
		int summarycolor = -1;
		char summsg[500];

		suburl = strstr(s->url, ".html");
		if (suburl) {
			suburl = strstr(s->url, getenv("BBWEB"));
			if (suburl) suburl += strlen(getenv("BBWEB"));
			if (suburl && (*suburl == '/')) suburl++;

			if ( suburl && 
			     (strcmp(suburl, "bb.html") != 0) &&
			     (strcmp(suburl, "bb2.html") != 0) &&
			     (strcmp(suburl, "index.html") != 0) ) {

				/* Specific page  - "suburl" is now either */
				/* "pagename.html" or "pagename/subpage.html" */
				char *p, *pg, *subpg;
				bbgen_page_t *pg1, *pg2;

				pg = subpg = NULL;
				pg1 = pg2 = NULL;

				pg = suburl; p = strchr(pg, '/');
				if (p) {
					*p = '\0';
					subpg = (p+1);
				}
				else
					subpg = NULL;

				for (pg1 = pagehead; (pg1 && (strcmp(pg1->name, pg) != 0)); pg1 = pg1->next) ;
				if (pg1 && subpg) {
					for (pg2 = pg1->subpages; (pg2 && (strcmp(pg2->name, subpg) != 0)); pg2 = pg2->next) ;
				}

				if (pg2) summarycolor = pg2->color;
				else if (pg1) summarycolor = pg1->color;
				else summarycolor = pagehead->color;
			}
		}

		if (summarycolor == -1) {
			summarycolor = pagehead->color;
		}

		/* Send the summary message */
		sprintf(summsg, "summary summary.%s %s %s %s",
			s->name, colorname(summarycolor), s->url, ctime(&now));

		childpid = fork();
		if (childpid == -1) {
			printf("Fork error\n");
		}
		else if (childpid == 0) {
			execl(bbcmd, "bb: bbd summary", s->receiver, summsg, NULL);
		}
		else {
			wait(NULL);
		}
	}
}

