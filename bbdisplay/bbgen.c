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

static char rcsid[] = "$Id: bbgen.c,v 1.51 2002-12-19 14:13:23 hstoerne Exp $";

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
#include "larrdgen.h"

/* Global vars */
page_t		*pagehead = NULL;			/* Head of page list */
link_t  	*linkhead = NULL;			/* Head of links list */
hostlist_t	*hosthead = NULL;			/* Head of hosts list */
state_t		*statehead = NULL;			/* Head of list of all state entries */
col_t   	*colhead = NULL;			/* Head of column-name list */
summary_t	*sumhead = NULL;			/* Summaries we send out */
dispsummary_t	*dispsums = NULL;			/* Summaries we received and display */


int main(int argc, char *argv[])
{
	char		pagedir[256];
	char		rrddir[256];
	page_t 		*p, *q;
	dispsummary_t	*s;
	int		i;

	sprintf(pagedir, "%s/www", getenv("BBHOME"));
	sprintf(rrddir, "%s/rrd", getenv("BBVAR"));

	for (i = 1; (i < argc); i++) {
		if (strcmp(argv[i], "--recentgifs") == 0) {
			use_recentgifs = 1;
		}
		else if (strncmp(argv[i], "--larrd=", 8) == 0) {
			char *lp = strchr(argv[i], '=');
			strcpy(larrdcol, (lp+1));
		}
		else if (strncmp(argv[i], "--rrddir=", 8) == 0) {
			char *lp = strchr(argv[i], '=');
			strcpy(rrddir, (lp+1));
		}
		else if (strcmp(argv[i], "--nopurple") == 0) {
			enable_purpleupd = 0;
		}
		else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-?") == 0)) {
			printf("Usage: %s [--options] [WebpageDirectory]\n", argv[0]);
			printf("Options:\n");
			printf("    --recentgifs           : Use xxx-recent.gif images\n");
			printf("    --larrd=LARRDCOLUMN    : LARRD data in column LARRDCOLUMN never goes purple\n");
			printf("    --rrddir=RRD-directory : Directory for LARRD RRD files\n");
			printf("    --nopurple             : Disable all purple updates\n");
			exit(1);
		}
		else {
			/* Last argument is pagedir */
			strcpy(pagedir, argv[1]);
		}
	}

	/* Load all data from the various files */
	linkhead = load_all_links();
	pagehead = load_bbhosts();

	/* Generate the LARRD pages before loading state */
	generate_larrd(rrddir, larrdcol);

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

	/* Send summary notices */
	send_summaries(sumhead);

	/* Load displayed summaries */
	dispsums = load_summaries();

	/* Recalc topmost page (background color for bb.html) */
	for (s=dispsums; (s); s = s->next) {
		if (s->color > pagehead->color) pagehead->color = s->color;
	}

	/* Generate pages */
	if (chdir(pagedir) != 0) {
		printf("Cannot change to webpage directory %s\n", pagedir);
		exit(1);
	}

	/* The main page - bb.html and pages/subpages thereunder */
	do_bb_page(pagehead, dispsums, "bb.html");

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

