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
/* This program is released under the GNU Public License (GPL), version 2.    */
/* See the file "COPYING" for details.                                        */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#include "bbgen.h"
#include "process.h"

void calc_hostcolors(hostlist_t *head)
{
	int		color;
	hostlist_t 	*h;
	entry_t		*e;

	for (h = head; (h); h = h->next) {
		color = 0;

		for (e = h->hostentry->entries; (e); e = e->next) {
			if (e->color > color) color = e->color;
		}

		/* Blue and clear is not propageted upwards */
		if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;

		h->hostentry->color = color;
	}
}

void calc_pagecolors(page_t *phead)
{
	page_t 	*p, *toppage;
	group_t *g;
	host_t  *h;
	int	color;

	for (toppage=phead; (toppage); toppage = toppage->next) {

		/* Start with the color of immediate hosts */
		color = (toppage->hosts ? toppage->hosts->color : -1);

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

