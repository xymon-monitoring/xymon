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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "bbgen.h"
#include "util.h"
#include "loaddata.h"
#include "process.h"
#include "pagegen.h"

/* Global vars */
page_t		*pagehead = NULL;			/* Head of page list */

link_t  	*linkhead = NULL;			/* Head of links list */
link_t		null_link = { "", "", "", NULL };	/* Null link for pages/hosts/whatever with no link */

hostlist_t	*hosthead = NULL;			/* Head of hosts list */
state_t		*statehead = NULL;			/* Head of list of all state entries */
col_t   	*colhead = NULL;			/* Head of column-name list */
col_t		null_column = { "", NULL };		/* Null column */

summary_t	*sumhead;


int main(int argc, char *argv[])
{
	char	pagedir[256];
	page_t *p, *q;

	if (argc > 1) {
		strcpy(pagedir, argv[1]);
	}
	else {
		sprintf(pagedir, "%s/www", getenv("BBHOME"));
	}

	/* Load all data from the various files */
	linkhead = load_all_links();
	pagehead = load_bbhosts();
	statehead = load_state();

	/* Calculate colors of hosts and pages */
	calc_hostcolors(hosthead);
	calc_pagecolors(pagehead);

	/* Topmost page (background color for bb.html) */
	for (p=pagehead; (p); p = p->next) {
		if (p->color > pagehead->color) pagehead->color = p->color;
	}

	/* Remove old acknowledgements */
	delete_old_acks();

	/* Generate pages */
	if (chdir(pagedir) != 0) {
		printf("Cannot change to webpage directory %s\n", pagedir);
		exit(1);
	}

	/* The main page - bb.html and pages/subpages thereunder */
	do_bb_page(pagehead, "bb.html");

	/* Do pages - contains links to subpages, groups, hosts */
	for (p=pagehead->next; (p); p = p->next) {
		char dirfn[256], fn[256];

		sprintf(dirfn, "%s", p->name);
		mkdir(dirfn, 0755);
		sprintf(fn, "%s/%s.html", dirfn, p->name);
		do_page(p, fn, p->name);

		/* Do subpages */
		for (q = p->subpages; (q); q = q->next) {
			sprintf(dirfn, "%s/%s", p->name, q->name);
			mkdir(dirfn, 0755);
			sprintf(fn, "%s/%s.html", dirfn, q->name);
			do_subpage(q, fn, p->name);
		}
	}

	/* The full summary page - bb2.html */
	do_bb2_page("bb2.html", 0);

	/* Reduced summary (alerts) page - bbnk.html */
	do_bb2_page("bbnk.html", 1);

	return 0;
}

