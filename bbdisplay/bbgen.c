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

static char rcsid[] = "$Id: bbgen.c,v 1.65 2003-02-05 15:55:49 henrik Exp $";

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
#include "infogen.h"
#include "alert.h"
#include "debug.h"

/* Global vars */
page_t		*pagehead = NULL;			/* Head of page list */
link_t  	*linkhead = NULL;			/* Head of links list */
hostlist_t	*hosthead = NULL;			/* Head of hosts list */
state_t		*statehead = NULL;			/* Head of list of all state entries */
col_t   	*colhead = NULL;			/* Head of column-name list */
summary_t	*sumhead = NULL;			/* Summaries we send out */
dispsummary_t	*dispsums = NULL;			/* Summaries we received and display */

char *reqenv[] = {
"BB",
"BBACKS",
"BBDISP",
"BBHIST",
"BBHISTLOGS",
"BBHOME",
"BBHOSTS",
"BBLOGS",
"BBLOGSTATUS",
"BBNOTES",
"BBREL",
"BBRELDATE",
"BBSKIN",
"BBTMP",
"BBVAR",
"BBWEB",
"CGIBINURL",
"DOTHEIGHT",
"DOTWIDTH",
"MACHINEADDR",
"MKBBCOLFONT",
"MKBBLOCAL",
"MKBBREMOTE",
"MKBBROWFONT",
"MKBBSUBLOCAL",
"MKBBTITLE",
"PURPLEDELAY",
NULL };

int main(int argc, char *argv[])
{
	char		pagedir[256];
	char		rrddir[256];
	page_t 		*p, *q;
	dispsummary_t	*s;
	int		i;
	int		pagegenstat;
	int		bbpageONLY = 0;

	sprintf(pagedir, "%s/www", getenv("BBHOME"));
	sprintf(rrddir, "%s/rrd", getenv("BBVAR"));
	init_timestamp();

	for (i = 1; (i < argc); i++) {
		if (strcmp(argv[i], "--recentgifs") == 0) {
			use_recentgifs = 1;
		}
		else if (strncmp(argv[i], "--larrdupdate=", 14) == 0) {
			char *lp = strchr(argv[i], '=');

			larrd_update_interval = atoi(lp);
			if (larrd_update_interval <= 0) enable_larrdgen=0;
			else enable_larrdgen = 1;
		}
		else if (strncmp(argv[i], "--larrd", 7) == 0) {
			/* "--larrd" just enable larrd page generation */
			/* "--larrd=xxx" does that, and redefines the larrd column name */
			char *lp = strchr(argv[i], '=');

			enable_larrdgen=1;
			if (lp) {
				strcpy(larrdcol, (lp+1));
			}
		}
		else if (strncmp(argv[i], "--infoupdate=", 13) == 0) {
			char *lp = strchr(argv[i], '=');

			info_update_interval = atoi(lp);
			if (info_update_interval <= 0) enable_infogen=0;
			else enable_infogen = 1;
		}
		else if (strncmp(argv[i], "--info", 6) == 0) {
			/* "--info" just enable info page generation */
			/* "--info=xxx" does that, and redefines the info column name */
			char *lp = strchr(argv[i], '=');

			enable_infogen=1;
			if (lp) {
				strcpy(infocol, (lp+1));
			}
		}
		else if (strncmp(argv[i], "--rrddir=", 9) == 0) {
			char *lp = strchr(argv[i], '=');
			strcpy(rrddir, (lp+1));
		}
		else if ((strncmp(argv[i], "--noprop=", 9) == 0) || (strncmp(argv[i], "--nopropyellow=", 15) == 0)) {
			char *lp = strchr(argv[i], '=');
			nopropyellowdefault = malloc(strlen(lp)+2);
			sprintf(nopropyellowdefault, ",%s,", (lp+1));
		}
		else if (strncmp(argv[i], "--nopropred=", 12) == 0) {
			char *lp = strchr(argv[i], '=');
			nopropreddefault = malloc(strlen(lp)+2);
			sprintf(nopropreddefault, ",%s,", (lp+1));
		}
		else if (strcmp(argv[i], "--nopurple") == 0) {
			enable_purpleupd = 0;
		}
		else if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[i], "--bbpageONLY") == 0) {
			bbpageONLY = 1;
		}
		else if (strncmp(argv[i], "--template=", 11) == 0) {
			char *lp = strchr(argv[i], '=');
			lp++;
			bb_headfoot = malloc(strlen(lp+1));
			strcpy(bb_headfoot, lp);
		}
		else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-?") == 0)) {
			printf("Usage: %s [options] [WebpageDirectory]\n", argv[0]);
			printf("Options:\n");
			printf("    --nopurple             : Disable purple status-updates\n");
			printf("    --recentgifs           : Use xxx-recent.gif icons for newly changed tests\n");
			printf("    --info[=INFOCOLUMN]    : Generate INFO data in column INFOCOLUMN\n");
			printf("    --infoupdate=N         : time between updates of INFO column pages in seconds\n");
			printf("\nLARRD support options:\n");
			printf("    --larrd[=LARRDCOLUMN]  : LARRD data in column LARRDCOLUMN, and handle larrd-html\n");
			printf("    --larrdupdate=N        : time between updates of LARRD pages in seconds\n");
			printf("    --rrddir=RRD-directory : Directory for LARRD RRD files\n");
			printf("\nStatus propagation control options:\n");
			printf("    --noprop=test[,test]   : Disable upwards status propagation when YELLOW\n");
			printf("    --nopropred=test[,test]: Disable upwards status propagation when RED or YELLOW\n");
			printf("\n");
			printf("    --template=TEMPLATE    : template for header and footer files\n");
			printf("    --bbpageONLY           : Standard page generation only\n");
#ifdef DEBUG
			printf("\n");
			printf("    --debug                : Dumps internal state-table\n");
#endif
			exit(1);
		}
		else {
			/* Last argument is pagedir */
			strcpy(pagedir, argv[i]);
		}
	}

	/* Check that all needed environment vars are defined */
	envcheck(reqenv);

	/*
	 * bbpageONLY means ONLY generate the standard pages.
	 * No NK page, no BB2 page, no LARRD, no INFO, no purple updates 
	 */
	if (bbpageONLY) enable_purpleupd = enable_larrdgen = enable_infogen = 0;

	/* Load all data from the various files */
	linkhead = load_all_links();
	pagehead = load_bbhosts();

	if (!bbpageONLY) {
		/* Generate the LARRD pages before loading state */
		pagegenstat = generate_larrd(rrddir, larrdcol);

		/* Dont generate both LARRD and info in one run */
		if (pagegenstat) pagegenstat = generate_info(infocol);
	}

	statehead = load_state();

	/* Calculate colors of hosts and pages */
	calc_hostcolors(hosthead);
	calc_pagecolors(pagehead);

	/* Topmost page (background color for bb.html) */
	for (p=pagehead; (p); p = p->next) {
		if (p->color > pagehead->color) pagehead->color = p->color;
	}

	/* Remove old acknowledgements */
	if (!bbpageONLY) delete_old_acks();

	/* Send summary notices */
	if (!bbpageONLY) send_summaries(sumhead);

	/* Load displayed summaries */
	dispsums = load_summaries();

	/* Recalc topmost page (background color for bb.html) */
	for (s=dispsums; (s); s = s->next) {
		if (s->color > pagehead->color) pagehead->color = s->color;
	}

	if (debug) dumpall(pagehead);

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
	if (!bbpageONLY) do_bb2_page("bb2.html", 0);

	/* Reduced summary (alerts) page - bbnk.html */
	if (!bbpageONLY) do_bb2_page("bbnk.html", 1);

	return 0;
}

