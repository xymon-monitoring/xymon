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

static char rcsid[] = "$Id: bbgen.c,v 1.93 2003-04-23 20:32:39 henrik Exp $";

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
#include "wmlgen.h"

/* Global vars */
bbgen_page_t	*pagehead = NULL;			/* Head of page list */
link_t  	*linkhead = NULL;			/* Head of links list */
hostlist_t	*hosthead = NULL;			/* Head of hosts list */
state_t		*statehead = NULL;			/* Head of list of all state entries */
col_t   	*colhead = NULL;			/* Head of column-name list */
summary_t	*sumhead = NULL;			/* Summaries we send out */
dispsummary_t	*dispsums = NULL;			/* Summaries we received and display */
int		bb_color, bb2_color, bbnk_color;	/* Top-level page colors */

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
#ifdef WMLSUPPORT
"WML_OUTPUT",
#endif
NULL };


int main(int argc, char *argv[])
{
	char		*pagedir;
	char		*rrddir;
	bbgen_page_t 	*p;
	dispsummary_t	*s;
	int		i;
	int		pagegenstat;
	int		bbpageONLY = 0;

	bb_color = bb2_color = bbnk_color = -1;
	pagedir = rrddir = NULL;
	init_timestamp();
	select_headers_and_footers("bb");

	for (i = 1; (i < argc); i++) {
		if (strcmp(argv[i], "--nopurple") == 0) {
			enable_purpleupd = 0;
		}
		else if (strncmp(argv[i], "--purplelifetime=", 17) == 0) {
			char *lp = strchr(argv[i], '=');

			purpledelay = atoi(lp+1);
			if (purpledelay < 0) purpledelay=0;
		}

		else if (strncmp(argv[i], "--ignorecolumns=", 16) == 0) {
			char *lp = strchr(argv[i], '=');
			ignorecolumns = malloc(strlen(lp)+2);
			sprintf(ignorecolumns, ",%s,", (lp+1));
		}
		else if (strncmp(argv[i], "--includecolumns=", 17) == 0) {
			char *lp = strchr(argv[i], '=');
			includecolumns = malloc(strlen(lp)+2);
			sprintf(includecolumns, ",%s,", (lp+1));
		}
		else if (strncmp(argv[i], "--doccgi=", 9) == 0) {
			char *lp = strchr(argv[i], '=');
			documentationcgi = malcop(lp+1);
		}

		else if (strcmp(argv[i], "--pages-first") == 0) {
			hostsbeforepages = 0;
		}
		else if (strcmp(argv[i], "--pages-last") == 0) {
			hostsbeforepages = 1;
		}
		else if (strncmp(argv[i], "--subpagecolumns=", 17) == 0) {
			char *lp = strchr(argv[i], '=');

			subpagecolumns = atoi(lp+1);
			if (subpagecolumns < 1) subpagecolumns=1;
		}
		else if (strcmp(argv[i], "--recentgifs") == 0) {
			use_recentgifs = 1;
		}
		else if (strcmp(argv[i], "--sort-group-only-items") == 0) {
			sort_grouponly_items = 1;
		}
		else if (strncmp(argv[i], "--page-title=", 13) == 0) {
			char *lp = strchr(argv[i], '=');

			defaultpagetitle = malcop(lp+1);
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

		else if (strncmp(argv[i], "--infoupdate=", 13) == 0) {
			char *lp = strchr(argv[i], '=');

			info_update_interval = atoi(lp+1);
			if (info_update_interval <= 0) enable_infogen=0;
			else enable_infogen = 1;
		}
		else if (strncmp(argv[i], "--info", 6) == 0) {
			/* "--info" just enable info page generation */
			/* "--info=xxx" does that, and redefines the info column name */
			char *lp = strchr(argv[i], '=');

			enable_infogen=1;
			if (lp) infocol = malcop(lp+1);
		}

		else if (strncmp(argv[i], "--larrdupdate=", 14) == 0) {
			char *lp = strchr(argv[i], '=');

			larrd_update_interval = atoi(lp+1);
			if (larrd_update_interval <= 0) enable_larrdgen=0;
			else enable_larrdgen = 1;
		}
		else if (strncmp(argv[i], "--larrdgraphs=", 14) == 0) {
			char *lp = strchr(argv[i], '=');

			enable_larrdgen=1;
			if (lp) larrdgraphs_default = malcop(lp+1);
		}
		else if (strncmp(argv[i], "--larrd", 7) == 0) {
			/* "--larrd" just enable larrd page generation */
			/* "--larrd=xxx" does that, and redefines the larrd column name */
			char *lp = strchr(argv[i], '=');

			enable_larrdgen=1;
			if (lp) larrdcol = malcop(lp+1);
		}
		else if (strncmp(argv[i], "--rrddir=", 9) == 0) {
			char *lp = strchr(argv[i], '=');
			rrddir = malcop(lp+1);
		}
		else if (strncmp(argv[i], "--log-nohost-rrds", 17) == 0) {
			log_nohost_rrds=1;
		}

		else if (strcmp(argv[i], "--bbpageONLY") == 0) {
			bbpageONLY = 1;
		}
		else if (strncmp(argv[i], "--template=", 11) == 0) {
			char *lp = strchr(argv[i], '=');
			lp++;
			select_headers_and_footers(lp);
		}

		else if (strcmp(argv[i], "--timing") == 0) {
			timing = 1;
		}
		else if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[i], "--version", 9) == 0) {
			printf("bbgen version %s\n", VERSION);
			exit(0);
		}

		else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-?") == 0)) {
			printf("bbgen version %s\n\n", VERSION);
			printf("Usage: %s [options] [WebpageDirectory]\n", argv[0]);
			printf("Options:\n");
			printf("    --nopurple                  : Disable purple status-updates\n");
			printf("    --purplelifetime=N          : Purple messages have a lifetime of N minutes\n");
			printf("    --ignorecolumns=test[,test] : Completely ignore these columns\n");
			printf("    --includecolumns=test[,test]: Always include these columns on bb2 page\n");
			printf("    --doccgi=cgibinURL          : Hostnames link to a general CGI script for docs\n");
			printf("\nPage layout options:\n");
			printf("    --pages-last                : Put page- and subpage-links after hosts (as BB does)\n");
			printf("    --pages-first               : Put page- and subpage-links before hosts (default)\n");
			printf("    --subpagecolumns=N          : Number of columns for links to pages and subpages\n");
			printf("    --recentgifs                : Use xxx-recent.gif icons for newly changed tests\n");
			printf("    --sort-group-only-items     : Display group-only items in alphabetical order\n");
			printf("    --page-title=TITLE          : Set a default page title for all pages\n");
			printf("\nStatus propagation control options:\n");
			printf("    --noprop=test[,test]        : Disable upwards status propagation when YELLOW\n");
			printf("    --nopropred=test[,test]     : Disable upwards status propagation when RED or YELLOW\n");
			printf("\nInfo column options:\n");
			printf("    --info[=INFOCOLUMN]         : Generate INFO data in column INFOCOLUMN\n");
			printf("    --infoupdate=N              : time between updates of INFO column pages in seconds\n");
			printf("\nLARRD support options:\n");
			printf("    --larrd[=LARRDCOLUMN]       : LARRD data in column LARRDCOLUMN, and handle larrd-html\n");
			printf("    --larrdgraphs=GRAPHSPEC     : Set a default value for the LARRD: bb-hosts tag\n");
			printf("    --larrdupdate=N             : time between updates of LARRD pages in seconds\n");
			printf("    --rrddir=RRD-directory      : Directory for LARRD RRD files\n");
			printf("\nAlternate pageset generation support:\n");
			printf("    --bbpageONLY                : Generate the standard (bb.html) page only\n");
			printf("    --template=TEMPLATE         : template for header and footer files\n");
			printf("\nDebugging options:\n");
			printf("    --timing                    : Collect timing information\n");
#ifdef DEBUG
			printf("    --debug                     : Debugging information\n");
#endif
			printf("    --version                   : Show version information\n");
			exit(0);
		}
		else if (strncmp(argv[i], "-", 1) == 0) {
			printf("Unknown option : %s\n", argv[i]);
		}

		else {
			/* Last argument is pagedir */
			pagedir = malloc(strlen(argv[i])+1);
			strcpy(pagedir, argv[i]);
		}
	}

	add_timestamp("Startup");

	/* Check that all needed environment vars are defined */
	envcheck(reqenv);

	if (pagedir == NULL) {
		pagedir = malloc(strlen(getenv("BBHOME"))+5);
		sprintf(pagedir, "%s/www", getenv("BBHOME"));
	}
	if (rrddir == NULL) {
		rrddir = malloc(strlen(getenv("BBVAR"))+5);
		sprintf(rrddir, "%s/rrd", getenv("BBVAR"));
	}


	/*
	 * bbpageONLY means ONLY generate the standard pages.
	 * No NK page, no BB2 page, no LARRD, no INFO, no purple updates 
	 */
	if (bbpageONLY) enable_purpleupd = enable_larrdgen = enable_infogen = 0;

	/* Load all data from the various files */
	linkhead = load_all_links();
	add_timestamp("Load links done");
	pagehead = load_bbhosts();
	add_timestamp("Load bbhosts done");

	if (!bbpageONLY) {
		/* Generate the LARRD pages before loading state */
		pagegenstat = generate_larrd(rrddir, larrdcol);
		add_timestamp("LARRD generate done");

		/* Dont generate both LARRD and info in one run */
		if (pagegenstat) pagegenstat = generate_info(infocol);
		add_timestamp("INFO generate done");
	}

	/* Remove old acknowledgements */
	if (!bbpageONLY) delete_old_acks();
	add_timestamp("ACK removal done");

	statehead = load_state(&dispsums);
	add_timestamp("Load STATE done");

	/* Calculate colors of hosts and pages */
	calc_hostcolors(hosthead);
	calc_pagecolors(pagehead);

	/* Topmost page (background color for bb.html) */
	for (p=pagehead; (p); p = p->next) {
		if (p->color > pagehead->color) pagehead->color = p->color;
	}
	bb_color = pagehead->color;

	/*
	 * Displayed summaries affect the BB page only, 
	 * but should not go into the color we report to
	 * others.
	 */
	for (s=dispsums; (s); s = s->next) {
		if (s->color > pagehead->color) pagehead->color = s->color;
	}
	add_timestamp("Color calculation done");


	if (debug) dumpall(pagehead);

	/* Generate pages */
	if (chdir(pagedir) != 0) {
		printf("Cannot change to webpage directory %s\n", pagedir);
		exit(1);
	}

	/* The main page - bb.html and pages/subpages thereunder */
	add_timestamp("BB pagegen start");
	do_page_with_subs(pagehead, dispsums);
	add_timestamp("BB pagegen done");

	/* The full summary page - bb2.html */
	if (!bbpageONLY) {
		bb2_color = do_bb2_page("bb2.html", PAGE_BB2);
		add_timestamp("BB2 generation done");
	}

	/* Reduced summary (alerts) page - bbnk.html */
	if (!bbpageONLY) {
		bbnk_color = do_bb2_page("bbnk.html", PAGE_NK);
		add_timestamp("BBNK generation done");
	}

	/* Send summary notices */
	if (!bbpageONLY) {
		send_summaries(sumhead);
		add_timestamp("Summary transmission done");
	}

#ifdef WMLSUPPORT
	/* Generate a hosts file for the WML generator */
	if (!bbpageONLY && (strcmp(getenv("WML_OUTPUT"), "TRUE") == 0)) {
		do_wml_cards(0);
		add_timestamp("WML generation done");
	}
#endif

	add_timestamp("Run completed");
	show_timestamps();

	return 0;
}

