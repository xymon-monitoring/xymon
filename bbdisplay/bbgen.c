/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* This is the main program for generating Hobbit overview webpages, showing  */
/* the status of hosts in a Hobbit system.                                    */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbgen.c,v 1.232 2008-01-03 09:40:31 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "version.h"

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "loadbbhosts.h"
#include "loaddata.h"
#include "process.h"
#include "pagegen.h"
#include "wmlgen.h"
#include "rssgen.h"
#include "bbconvert.h"
#include "csvreport.h"

#include <signal.h>


/* Global vars */
bbgen_page_t	*pagehead = NULL;			/* Head of page list */
state_t		*statehead = NULL;			/* Head of list of all state entries */
summary_t	*sumhead = NULL;			/* Summaries we send out */
dispsummary_t	*dispsums = NULL;			/* Summaries we received and display */
int		bb_color, bb2_color, bbnk_color;	/* Top-level page colors */
int		fqdn = 1;				/* Hobbit FQDN setting */

time_t		reportstart = 0;
time_t		reportend = 0;
double		reportwarnlevel = 97.0;
double		reportgreenlevel = 99.995;
int		reportstyle = STYLE_CRIT;
int		dynamicreport = 1;
enum tooltipuse_t tooltipuse = TT_BBONLY;

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
"BBREP",
"BBREPURL",
"BBSKIN",
"BBTMP",
"BBVAR",
"BBWEB",
"BBWEBHOST",
"BBWEBHOSTURL",
"CGIBINURL",
"DOTHEIGHT",
"DOTWIDTH",
"MACHINE",
"MACHINEADDR",
"MKBBCOLFONT",
"MKBBLOCAL",
"MKBBSUBLOCAL",
"MKBBREMOTE",
"MKBBROWFONT",
"MKBBTITLE",
"PURPLEDELAY",
NULL };


int main(int argc, char *argv[])
{
	char		*pagedir;
	bbgen_page_t 	*p;
	dispsummary_t	*s;
	int		i;
	char		*pageset = NULL;
	char		*nssidebarfilename = NULL;
	char		*egocolumn = NULL;
	char		*csvfile = NULL;
	char		csvdelim = ',';
	int		embedded = 0;
	int		hobbitddump = 0;
	char		*envarea = NULL;
	int		do_normal_pages = 1;

	/* Setup standard header+footer (might be modified by option pageset) */
	select_headers_and_footers("bb");

	bb_color = bb2_color = bbnk_color = -1;
	pagedir = NULL;
	init_timestamp();
	fqdn = get_fqdn();

	/* Setup values from env. vars that may be overridden via commandline options */
	if (xgetenv("MKBB2COLREPEAT")) {
		int i = atoi(xgetenv("MKBB2COLREPEAT"));

		if (i > 0) maxrowsbeforeheading = i;
	}

	for (i = 1; (i < argc); i++) {
		if ( (strcmp(argv[i], "--hobbitd") == 0)       ||
		     (argnmatch(argv[i], "--purplelifetime=")) ||
		     (strcmp(argv[i], "--nopurple") == 0)      ) {
			/* Deprecated */
		}
		else if (argnmatch(argv[i], "--env=")) {
			char *lp = strchr(argv[i], '=');
			loadenv(lp+1, envarea);
		}
		else if (argnmatch(argv[i], "--area=")) {
			char *lp = strchr(argv[i], '=');
			envarea = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--hobbitddump")) {
			hobbitddump = 1;
		}

		else if (argnmatch(argv[i], "--ignorecolumns=")) {
			char *lp = strchr(argv[i], '=');
			ignorecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(ignorecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--nk-reds-only")) {
			nkonlyreds = 1;
		}
		else if (argnmatch(argv[i], "--bb2-ignorecolumns=")) {
			char *lp = strchr(argv[i], '=');
			bb2ignorecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(bb2ignorecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--bb2-colors=")) {
			char *lp = strchr(argv[i], '=') + 1;
			bb2colors = colorset(lp, (1 << COL_GREEN));
		}
		else if (argnmatch(argv[i], "--bb2-ignorepurples")) {
			bb2colors = (bb2colors & ~(1 << COL_PURPLE));
		}
		else if (argnmatch(argv[i], "--bb2-ignoredialups")) {
			bb2nodialups = 1;
		}
		else if (argnmatch(argv[i], "--includecolumns=")) {
			char *lp = strchr(argv[i], '=');
			includecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(includecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--eventignore=")) {
			char *lp = strchr(argv[i], '=');
			eventignorecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(eventignorecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--doccgi=")) {
			char *lp = strchr(argv[i], '=');
			char *url = (char *)malloc(strlen(xgetenv("CGIBINURL"))+strlen(lp+1)+2);
			sprintf(url, "%s/%s", xgetenv("CGIBINURL"), lp+1);
			setdocurl(url);
			xfree(url);
		}
		else if (argnmatch(argv[i], "--docurl=")) {
			char *lp = strchr(argv[i], '=');
			setdocurl(lp+1);
		}
		else if (argnmatch(argv[i], "--no-doc-window")) {
			/* This is a no-op now */
		}
		else if (argnmatch(argv[i], "--doc-window")) {
			setdocurl("TARGET=\"_blank\"");
		}
		else if (argnmatch(argv[i], "--htmlextension=")) {
			char *lp = strchr(argv[i], '=');
			htmlextension = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--htaccess")) {
			char *lp = strchr(argv[i], '=');
			if (lp) htaccess = strdup(lp+1);
			else htaccess = ".htaccess";
		}
		else if ((strcmp(argv[i], "--wml") == 0) || argnmatch(argv[i], "--wml=")) {
			char *lp = strchr(argv[i], '=');

			if (lp) {
				wapcolumns = (char *) malloc(strlen(lp)+2);
				sprintf(wapcolumns, ",%s,", (lp+1));
			}
			enable_wmlgen = 1;
		}
		else if (argnmatch(argv[i], "--nstab=")) {
			char *lp = strchr(argv[i], '=');

			if (strlen(lp+1) > 0) {
				nssidebarfilename = strdup(lp+1);
			}
			else errprintf("--nstab requires a filename\n");
		}
		else if (argnmatch(argv[i], "--nslimit=")) {
			char *lp = strchr(argv[i], '=');
			nssidebarcolorlimit = parse_color(lp+1);
		}
		else if (argnmatch(argv[i], "--rssversion=")) {
			char *lp = strchr(argv[i], '=');

			rssversion = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--rsslimit=")) {
			char *lp = strchr(argv[i], '=');
			rsscolorlimit = parse_color(lp+1);
		}
		else if (argnmatch(argv[i], "--rss")) {
			wantrss = 1;
		}
		else if (argnmatch(argv[i], "--rssextension=")) {
			char *lp = strchr(argv[i], '=');
			rssextension = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--reportopts=")) {
			char style[MAX_LINE_LEN];
			unsigned int rstart, rend;

			int count = sscanf(argv[i], "--reportopts=%u:%u:%d:%s", 
					   &rstart, &rend, &dynamicreport, style);
			reportstart = rstart; reportend = rend;

			if (count < 2) {
				errprintf("Invalid --reportopts option: Must have start- and end-times\n");
				return 1;
			}

			if (count < 3) dynamicreport = 1;
			if (count == 4) {
				if (strcmp(style, stylenames[STYLE_CRIT]) == 0) reportstyle = STYLE_CRIT;
				else if (strcmp(style, stylenames[STYLE_NONGR]) == 0) reportstyle = STYLE_NONGR;
				else reportstyle = STYLE_OTHER;
			}

			if (reportstart < 788918400) reportstart = 788918400;
			if (reportend > getcurrenttime(NULL)) reportend = getcurrenttime(NULL);

			if (xgetenv("BBREPWARN")) reportwarnlevel = atof(xgetenv("BBREPWARN"));
			if (xgetenv("BBREPGREEN")) reportgreenlevel = atof(xgetenv("BBREPGREEN"));

			if ((reportwarnlevel < 0.0) || (reportwarnlevel > 100.0)) reportwarnlevel = 97.0;
			if ((reportgreenlevel < 0.0) || (reportgreenlevel > 100.0)) reportgreenlevel = 99.995;

			select_headers_and_footers("bbrep");
			sethostenv_report(reportstart, reportend, reportwarnlevel, reportgreenlevel);
		}
		else if (argnmatch(argv[i], "--csv="))  {
			char *lp = strchr(argv[i], '=');
			csvfile = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--csvdelim="))  {
			char *lp = strchr(argv[i], '=');
			csvdelim = *(lp+1);
		}
		else if (argnmatch(argv[i], "--snapshot=")) {
			char *lp = strchr(argv[i], '=');

			snapshot = atol(lp+1);
			select_headers_and_footers("bbsnap");
			sethostenv_snapshot(snapshot);
		}

		else if (strcmp(argv[i], "--pages-first") == 0) {
			hostsbeforepages = 0;
		}
		else if (strcmp(argv[i], "--pages-last") == 0) {
			hostsbeforepages = 1;
		}
		else if (argnmatch(argv[i], "--subpagecolumns=")) {
			char *lp = strchr(argv[i], '=');

			subpagecolumns = atoi(lp+1);
			if (subpagecolumns < 1) subpagecolumns=1;
		}
		else if (argnmatch(argv[i], "--maxrows=")) {
			char *lp = strchr(argv[i], '=');

			maxrowsbeforeheading = atoi(lp+1);
			if (maxrowsbeforeheading < 0) maxrowsbeforeheading=0;
		}
		else if (strcmp(argv[i], "--recentgifs") == 0) {
			use_recentgifs = 1;
		}
		else if (argnmatch(argv[i], "--recentgifs=")) {
			char *lp = strchr(argv[i], '=');

			use_recentgifs = 1;
			recentgif_limit = 60*durationvalue(lp+1);
		}
		else if (strcmp(argv[i], "--sort-group-only-items") == 0) {
			sort_grouponly_items = 1;
		}
		else if (argnmatch(argv[i], "--page-title=")) {
			char *lp = strchr(argv[i], '=');

			defaultpagetitle = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--dialupskin=")) {
			char *lp = strchr(argv[i], '=');

			dialupskin = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--reverseskin=")) {
			char *lp = strchr(argv[i], '=');

			reverseskin = strdup(lp+1);
		}
		else if (strcmp(argv[i], "--pagetitle-links") == 0) {
			pagetitlelinks = 1;
		}
		else if (strcmp(argv[i], "--pagetext-headings") == 0) {
			pagetextheadings = 1;
		}
		else if (strcmp(argv[i], "--underline-headings") == 0) {
			underlineheadings = 1;
		}
		else if (strcmp(argv[i], "--no-underline-headings") == 0) {
			underlineheadings = 0;
		}
		else if (strcmp(argv[i], "--no-eventlog") == 0) {
			bb2eventlog = 0;
		}
		else if (argnmatch(argv[i], "--max-eventcount=")) {
			char *lp = strchr(argv[i], '=');

			bb2eventlogmaxcount = atoi(lp+1);
		}
		else if (argnmatch(argv[i], "--max-eventtime=")) {
			char *lp = strchr(argv[i], '=');

			bb2eventlogmaxtime = atoi(lp+1);
		}
		else if (argnmatch(argv[i], "--max-ackcount=")) {
			char *lp = strchr(argv[i], '=');

			bb2acklogmaxcount = atoi(lp+1);
		}
		else if (argnmatch(argv[i], "--max-acktime=")) {
			char *lp = strchr(argv[i], '=');

			bb2acklogmaxtime = atoi(lp+1);
		}
		else if (strcmp(argv[i], "--no-acklog") == 0) {
			bb2acklog = 0;
		}
		else if (strcmp(argv[i], "--no-pages") == 0) {
			do_normal_pages = 0;
		}

		else if (argnmatch(argv[i], "--noprop=")) {
			char *lp = strchr(argv[i], '=');
			nopropyellowdefault = (char *) malloc(strlen(lp)+2);
			sprintf(nopropyellowdefault, ",%s,", (lp+1));
			errprintf("--noprop is deprecated - use --nopropyellow instead\n");
		}
		else if (argnmatch(argv[i], "--nopropyellow=")) {
			char *lp = strchr(argv[i], '=');
			nopropyellowdefault = (char *) malloc(strlen(lp)+2);
			sprintf(nopropyellowdefault, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--nopropred=")) {
			char *lp = strchr(argv[i], '=');
			nopropreddefault = (char *) malloc(strlen(lp)+2);
			sprintf(nopropreddefault, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--noproppurple=")) {
			char *lp = strchr(argv[i], '=');
			noproppurpledefault = (char *) malloc(strlen(lp)+2);
			sprintf(noproppurpledefault, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--nopropack=")) {
			char *lp = strchr(argv[i], '=');
			nopropackdefault = (char *) malloc(strlen(lp)+2);
			sprintf(nopropackdefault, ",%s,", (lp+1));
		}

		else if (strcmp(argv[i], "--bbpageONLY") == 0) {
			/* Deprecated */
			errprintf("--bbpageONLY is deprecated - use --pageset=NAME to generate pagesets\n");
		}
		else if (strcmp(argv[i], "--embedded") == 0) {
			embedded = 1;
		}
		else if (argnmatch(argv[i], "--pageset=")) {
			char *lp = strchr(argv[i], '=');
			pageset = strdup(lp+1);
		}
		else if (argnmatch(argv[i], "--template=")) {
			char *lp = strchr(argv[i], '=');
			lp++;
			select_headers_and_footers(lp);
		}

		else if (argnmatch(argv[i], "--tooltips=")) {
			char *lp = strchr(argv[i], '=');
			lp++;
			if (strcmp(lp, "always") == 0) tooltipuse = TT_ALWAYS;
			else if (strcmp(lp, "never") == 0) tooltipuse = TT_NEVER;
			else tooltipuse = TT_BBONLY;
		}

		else if (argnmatch(argv[i], "--purplelog=")) {
			char *lp = strchr(argv[i], '=');
			if (*(lp+1) == '/') purplelogfn = strdup(lp+1);
			else {
				purplelogfn = (char *) malloc(strlen(xgetenv("BBHOME"))+1+strlen(lp+1)+1);
				sprintf(purplelogfn, "%s/%s", xgetenv("BBHOME"), (lp+1));
			}
		}
		else if (argnmatch(argv[i], "--report=") || (strcmp(argv[i], "--report") == 0)) {
			char *lp = strchr(argv[i], '=');
			if (lp) {
				egocolumn = strdup(lp+1);
			}
			else egocolumn = "bbgen";
			timing = 1;
		}
		else if (argnmatch(argv[i], "--nklog=") || (strcmp(argv[i], "--nklog") == 0)) {
			char *lp = strchr(argv[i], '=');
			if (lp) {
				lognkstatus = strdup(lp+1);
			}
			else lognkstatus = "nk";
		}
		else if (strcmp(argv[i], "--timing") == 0) {
			timing = 1;
		}
		else if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[i], "--no-update") == 0) {
			dontsendmessages = 1;
		}
		else if (strcmp(argv[i], "--version") == 0) {
			printf("bbgen version %s\n", VERSION);
			printf("\n");
			exit(0);
		}

		else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-?") == 0)) {
			printf("bbgen for Hobbit version %s\n\n", VERSION);
			printf("Usage: %s [options] [WebpageDirectory]\n", argv[0]);
			printf("Options:\n");
			printf("    --ignorecolumns=test[,test] : Completely ignore these columns\n");
			printf("    --nk-reds-only              : Only show red statuses on the NK page\n");
			printf("    --bb2-ignorecolumns=test[,test]: Ignore these columns for the BB2 page\n");
			printf("    --bb2-ignorepurples         : Ignore all-purple hosts on BB2 page\n");
			printf("    --includecolumns=test[,test]: Always include these columns on BB2 page\n");
		        printf("    --max-eventcount=N          : Max number of events to include in eventlog\n");
		        printf("    --max-eventtime=N           : Show events that occurred within the last N minutes\n");
			printf("    --eventignore=test[,test]   : Columns to ignore in bb2 event-log display\n");
			printf("    --no-eventlog               : Do not generate the bb2 eventlog display\n");
			printf("    --no-acklog                 : Do not generate the bb2 ack-log display\n");
			printf("    --no-pages                  : Generate only the bb2 and bbnk pages\n");
			printf("    --docurl=documentation-URL  : Hostnames link to a general (dynamic) web page for docs\n");
			printf("    --no-doc-window             : Open doc-links in same window\n");
			printf("    --htmlextension=.EXT        : Sets filename extension for generated file (default: .html\n");
			printf("    --report[=COLUMNNAME]       : Send a status report about the running of bbgen\n");
			printf("    --reportopts=ST:END:DYN:STL : Run in Hobbit Reporting mode\n");
			printf("    --csv=FILENAME              : For Hobbit Reporting, output CSV file\n");
			printf("    --csvdelim=CHARACTER        : Delimiter in CSV file output (default: comma)\n");
			printf("    --snapshot=TIME             : Snapshot mode\n");
			printf("\nPage layout options:\n");
			printf("    --pages-first               : Put page- and subpage-links before hosts (default)\n");
			printf("    --pages-last                : Put page- and subpage-links after hosts\n");
			printf("    --subpagecolumns=N          : Number of columns for links to pages and subpages\n");
			printf("    --maxrows=N                 : Repeat column headings for every N hosts shown\n");
			printf("    --recentgifs                : Use xxx-recent.gif icons for newly changed tests\n");
			printf("    --sort-group-only-items     : Display group-only items in alphabetical order\n");
			printf("    --page-title=TITLE          : Set a default page title for all pages\n");
			printf("    --dialupskin=URL            : Use a different icon skin for dialup tests\n");
			printf("    --reverseskin=URL           : Use a different icon skin for reverse tests\n");
			printf("    --pagetitle-links           : Make page- and subpage-titles act as links\n");
			printf("    --pagetext-headings         : Use page texts as headings\n");
			printf("    --no-underline-headings     : Do not underline the page headings\n");
			printf("\nStatus propagation control options:\n");
			printf("    --noprop=test[,test]        : Disable upwards status propagation when YELLOW\n");
			printf("    --nopropred=test[,test]     : Disable upwards status propagation when RED or YELLOW\n");
			printf("    --noproppurple=test[,test]  : Disable upwards status propagation when PURPLE\n");
			printf("\nAlternate pageset generation support:\n");
			printf("    --pageset=SETNAME           : Generate non-standard pageset with tag SETNAME\n");
			printf("    --template=TEMPLATE         : template for header and footer files\n");
			printf("\nAlternate output formats:\n");
			printf("    --wml[=test1,test2,...]     : Generate a small (bb2-style) WML page\n");
			printf("    --nstab=FILENAME            : Generate a Netscape Sidebar feed\n");
			printf("    --nslimit=COLOR             : Minimum color to include on Netscape sidebar\n");
			printf("    --rss                       : Generate a RSS/RDF feed of alerts\n");
			printf("    --rssextension=.EXT         : Sets filename extension for RSS files (default: .rss\n");
			printf("    --rssversion={0.91|0.92|1.0|2.0} : Specify RSS/RDF version (default: 0.91)\n");
			printf("    --rsslimit=COLOR            : Minimum color to include on RSS feed\n");
			printf("\nDebugging/troubleshooting options:\n");
			printf("    --timing                    : Collect timing information\n");
			printf("    --debug                     : Debugging information\n");
			printf("    --version                   : Show version information\n");
			printf("    --purplelog=FILENAME        : Create a log of purple hosts and tests\n");
			exit(0);
		}
		else if (argnmatch(argv[i], "-")) {
			errprintf("Unknown option : %s\n", argv[i]);
		}

		else {
			/* Last argument is pagedir */
			pagedir = strdup(argv[i]);
		}
	}

	/* In case they changed the name of our column ... */
	if (egocolumn) setup_signalhandler(egocolumn);

	if (debug) {
		int i;
		printf("Command: bbgen");
		for (i=1; (i<argc); i++) printf(" '%s'", argv[i]);
		printf("\n");
		printf("Environment BBHOSTS='%s'\n", textornull(xgetenv("BBHOSTS")));
		printf("\n");
	}

	add_timestamp("Startup");

	/* Check that all needed environment vars are defined */
	envcheck(reqenv);

	/* Catch a SEGV fault */
	setup_signalhandler("bbgen");

	/* Set umask to 0022 so that the generated HTML pages have world-read access */
	umask(0022);

	if (pagedir == NULL) {
		if (xgetenv("BBWWW")) {
			pagedir = strdup(xgetenv("BBWWW"));
		}
		else {
			pagedir = (char *) malloc(strlen(xgetenv("BBHOME"))+5);
			sprintf(pagedir, "%s/www", xgetenv("BBHOME"));
		}
	}

	if (xgetenv("BBHTACCESS")) bbhtaccess = strdup(xgetenv("BBHTACCESS"));
	if (xgetenv("BBPAGEHTACCESS")) bbpagehtaccess = strdup(xgetenv("BBPAGEHTACCESS"));
	if (xgetenv("BBSUBPAGEHTACCESS")) bbsubpagehtaccess = strdup(xgetenv("BBSUBPAGEHTACCESS"));

	/*
	 * When doing embedded- or snapshow-pages, dont build the WML/RSS pages.
	 */
	if (embedded || snapshot) enable_wmlgen = wantrss = 0;
	if (embedded) {
		egocolumn = htaccess = NULL;

		/*
		 * Need to have default SIGPIPE handling when doing embedded stuff.
		 * We are probably run from a CGI script or something similar.
		 */
		signal(SIGPIPE, SIG_DFL);
	}

	/* Load all data from the various files */
	load_all_links();
	add_timestamp("Load links done");
	pagehead = load_bbhosts(pageset);
	add_timestamp("Load bbhosts done");

	if (!embedded) {
		/* Remove old acknowledgements */
		delete_old_acks();
		add_timestamp("ACK removal done");
	}

	statehead = load_state(&dispsums);
	if (embedded || snapshot) dispsums = NULL;
	add_timestamp("Load STATE done");

	if (hobbitddump) {
		dump_hobbitdchk();
		return 0;
	}

	/* Calculate colors of hosts and pages */
	calc_hostcolors(bb2ignorecolumns);
	calc_pagecolors(pagehead);

	/* Topmost page (background color for bb.html) */
	for (p=pagehead; (p); p = p->next) {
		if (p->color > pagehead->color) pagehead->color = p->color;
	}
	bb_color = pagehead->color;

	if (xgetenv("SUMMARY_SET_BKG") && (strcmp(xgetenv("SUMMARY_SET_BKG"), "TRUE") == 0)) {
		/*
		 * Displayed summaries affect the BB page only, 
		 * but should not go into the color we report to
		 * others.
		 */
		for (s=dispsums; (s); s = s->next) {
			if (s->color > pagehead->color) pagehead->color = s->color;
		}
	}
	add_timestamp("Color calculation done");

	if (debug) dumpall(pagehead);

	/* Generate pages */
	if (chdir(pagedir) != 0) {
		errprintf("Cannot change to webpage directory %s\n", pagedir);
		exit(1);
	}

	if (embedded) {
		/* Just generate that one page */
		do_one_page(pagehead, NULL, 1);
		return 0;
	}

	/* The main page - bb.html and pages/subpages thereunder */
	add_timestamp("Hobbit pagegen start");
	if (reportstart && csvfile) {
		csv_availability(csvfile, csvdelim);
	}
	if (do_normal_pages) {
		do_page_with_subs(pagehead, dispsums);
	}
	add_timestamp("Hobbit pagegen done");

	if (reportstart) {
		/* Reports end here */
		return 0;
	}

	/* The full summary page - bb2.html */
	bb2_color = do_bb2_page(nssidebarfilename, PAGE_BB2);
	add_timestamp("BB2 generation done");

	/* Reduced summary (alerts) page - bbnk.html */
	bbnk_color = do_bb2_page(NULL, PAGE_NK);
	add_timestamp("BBNK generation done");

	if (snapshot) {
		/* Snapshots end here */
		return 0;
	}

	/* Send summary notices - only once, so not on pagesets */
	if (pageset == NULL) {
		send_summaries(sumhead);
		add_timestamp("Summary transmission done");
	}

	/* Generate WML cards */
	if (enable_wmlgen) {
		do_wml_cards(pagedir);
		add_timestamp("WML generation done");
	}

	/* Need to do this before sending in our report */
	add_timestamp("Run completed");

	/* Tell about us */
	if (egocolumn) {
		char msgline[4096];
		char *timestamps;
		long bbsleep = (xgetenv("BBSLEEP") ? atol(xgetenv("BBSLEEP")) : 300);
		int color;

		/* Go yellow if it runs for too long */
		if (total_runtime() > bbsleep) {
			errprintf("WARNING: Runtime %ld longer than BBSLEEP (%ld)\n", total_runtime(), bbsleep);
		}
		color = (errbuf ? COL_YELLOW : COL_GREEN);

		combo_start();
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n\n", xgetenv("MACHINE"), egocolumn, colorname(color), timestamp);
		addtostatus(msgline);

		sprintf(msgline, "bbgen for Hobbit version %s\n", VERSION);
		addtostatus(msgline);

		addtostatus("\nStatistics:\n");
		sprintf(msgline, " Hosts                      : %5d\n", hostcount);
		addtostatus(msgline);
		sprintf(msgline, " Pages                      : %5d\n", pagecount);
		addtostatus(msgline);
		sprintf(msgline, " Status messages            : %5d\n", statuscount);
		addtostatus(msgline);
		sprintf(msgline, " - Red                      : %5d (%5.2f %%)\n", 
			colorcount[COL_RED], ((100.0 * colorcount[COL_RED]) / statuscount));
		addtostatus(msgline);
		sprintf(msgline, " - Red (non-propagating)    : %5d (%5.2f %%)\n", 
			colorcount_noprop[COL_RED], ((100.0 * colorcount_noprop[COL_RED]) / statuscount));
		addtostatus(msgline);
		sprintf(msgline, " - Yellow                   : %5d (%5.2f %%)\n", 
			colorcount[COL_YELLOW], ((100.0 * colorcount[COL_YELLOW]) / statuscount));
		addtostatus(msgline);
		sprintf(msgline, " - Yellow (non-propagating) : %5d (%5.2f %%)\n", 
			colorcount_noprop[COL_YELLOW], ((100.0 * colorcount_noprop[COL_YELLOW]) / statuscount));
		addtostatus(msgline);
		sprintf(msgline, " - Clear                    : %5d (%5.2f %%)\n", 
			colorcount[COL_CLEAR], ((100.0 * colorcount[COL_CLEAR]) / statuscount));
		addtostatus(msgline);
		sprintf(msgline, " - Green                    : %5d (%5.2f %%)\n", 
			colorcount[COL_GREEN], ((100.0 * colorcount[COL_GREEN]) / statuscount));
		addtostatus(msgline);
		sprintf(msgline, " - Purple                   : %5d (%5.2f %%)\n", 
			colorcount[COL_PURPLE], ((100.0 * colorcount[COL_PURPLE]) / statuscount));
		addtostatus(msgline);
		sprintf(msgline, " - Blue                     : %5d (%5.2f %%)\n", 
			colorcount[COL_BLUE], ((100.0 * colorcount[COL_BLUE]) / statuscount));
		addtostatus(msgline);

		if (errbuf) {
			addtostatus("\n\nError output:\n");
			addtostatus(errbuf);
		}

		show_timestamps(&timestamps);
		addtostatus(timestamps);

		finish_status();
		combo_end();
	}
	else show_timestamps(NULL);

	return 0;
}

