/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* This is the main program for generating Xymon overview webpages, showing   */
/* the status of hosts in a Xymon system.                                     */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include "version.h"

#include "xymongen.h"
#include "util.h"
#include "debug.h"
#include "loadlayout.h"
#include "loaddata.h"
#include "process.h"
#include "pagegen.h"
#include "wmlgen.h"
#include "rssgen.h"
#include "csvreport.h"

/* Global vars */
xymongen_page_t	*pagehead = NULL;			/* Head of page list */
state_t		*statehead = NULL;			/* Head of list of all state entries */
summary_t	*sumhead = NULL;			/* Summaries we send out */
dispsummary_t	*dispsums = NULL;			/* Summaries we received and display */
int		xymon_color, nongreen_color, critical_color;	/* Top-level page colors */
int		fqdn = 1;				/* Xymon FQDN setting */

time_t		reportstart = 0;
time_t		reportend = 0;
double		reportwarnlevel = 97.0;
double		reportgreenlevel = 99.995;
int		reportwarnstops = -1;
int		reportstyle = STYLE_CRIT;
int		dynamicreport = 1;
enum tooltipuse_t tooltipuse = TT_STDONLY;

char *reqenv[] = {
"XYMONACKDIR",
"XYMONHISTDIR",
"XYMONHISTLOGS",
"XYMONHOME",
"HOSTSCFG",
"XYMONRAWSTATUSDIR",
"XYMONLOGSTATUS",
"XYMONNOTESDIR",
"XYMONREPDIR",
"XYMONREPURL",
"XYMONSKIN",
"XYMONTMP",
"XYMONVAR",
"XYMONWEB",
"XYMONWEBHOST",
"XYMONWEBHOSTURL",
"CGIBINURL",
"DOTHEIGHT",
"DOTWIDTH",
"MACHINE",
"MACHINEADDR",
"XYMONPAGECOLFONT",
"XYMONPAGELOCAL",
"XYMONPAGESUBLOCAL",
"XYMONPAGEREMOTE",
"XYMONPAGEROWFONT",
"XYMONPAGETITLE",
"PURPLEDELAY",
NULL };


int main(int argc, char *argv[])
{
	char		*pagedir;
	xymongen_page_t 	*p;
	dispsummary_t	*s;
	int		i;
	char		*pageset = NULL;
	char		*nssidebarfilename = NULL;
	char		*egocolumn = NULL;
	char		*csvfile = NULL;
	char		csvdelim = ',';
	int		embedded = 0;
	char		*envarea = NULL;
	int		do_normal = 1;
	int		do_nongreen = 1;

	/* Setup standard header+footer (might be modified by option pageset) */
	select_headers_and_footers("std");

	xymon_color = nongreen_color = critical_color = -1;
	pagedir = NULL;
	init_timestamp();
	fqdn = get_fqdn();

	/* Setup values from env. vars that may be overridden via command-line options */
	if (xgetenv("XYMONPAGECOLREPEAT")) {
		int i = atoi(xgetenv("XYMONPAGECOLREPEAT"));

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

		else if (argnmatch(argv[i], "--ignorecolumns=")) {
			char *lp = strchr(argv[i], '=');
			ignorecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(ignorecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--critical-reds-only") || argnmatch(argv[i], "--nk-reds-only")) {
			critonlyreds = 1;
		}
		else if (argnmatch(argv[i], "--nongreen-ignorecolumns=") || argnmatch(argv[i], "--bb2-ignorecolumns=")) {
			char *lp = strchr(argv[i], '=');
			nongreenignorecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(nongreenignorecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--nongreen-colors=") || argnmatch(argv[i], "--bb2-colors=")) {
			char *lp = strchr(argv[i], '=') + 1;
			nongreencolors = colorset(lp, (1 << COL_GREEN));
		}
		else if (argnmatch(argv[i], "--nongreen-ignorepurples") || argnmatch(argv[i], "--bb2-ignorepurples")) {
			nongreencolors = (nongreencolors & ~(1 << COL_PURPLE));
		}
		else if (argnmatch(argv[i], "--nongreen-ignoredialups") || argnmatch(argv[i], "--bb2-ignoredialups")) {
			nongreennodialups = 1;
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

			if (xgetenv("XYMONREPWARN")) reportwarnlevel = atof(xgetenv("XYMONREPWARN"));
			if (xgetenv("XYMONREPGREEN")) reportgreenlevel = atof(xgetenv("XYMONREPGREEN"));

			if ((reportwarnlevel < 0.0) || (reportwarnlevel > 100.0)) reportwarnlevel = 97.0;
			if ((reportgreenlevel < 0.0) || (reportgreenlevel > 100.0)) reportgreenlevel = 99.995;

			select_headers_and_footers("rep");
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
			select_headers_and_footers("snap");
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
			nongreeneventlog = 0;
		}
		else if (argnmatch(argv[i], "--max-eventcount=")) {
			char *lp = strchr(argv[i], '=');

			nongreeneventlogmaxcount = atoi(lp+1);
		}
		else if (argnmatch(argv[i], "--max-eventtime=")) {
			char *lp = strchr(argv[i], '=');

			nongreeneventlogmaxtime = atoi(lp+1);
		}
		else if (argnmatch(argv[i], "--max-ackcount=")) {
			char *lp = strchr(argv[i], '=');

			nongreenacklogmaxcount = atoi(lp+1);
		}
		else if (argnmatch(argv[i], "--max-acktime=")) {
			char *lp = strchr(argv[i], '=');

			nongreenacklogmaxtime = atoi(lp+1);
		}
		else if (strcmp(argv[i], "--no-acklog") == 0) {
			nongreenacklog = 0;
		}
		else if (strcmp(argv[i], "--no-pages") == 0) {
			do_normal = 0;
		}
		else if ((strcmp(argv[i], "--no-nongreen") == 0) || (strcmp(argv[i], "--no-bb2") == 0)) {
			do_nongreen = 0;
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
			else tooltipuse = TT_STDONLY;
		}

		else if (argnmatch(argv[i], "--purplelog=")) {
			char *lp = strchr(argv[i], '=');
			if (*(lp+1) == '/') purplelogfn = strdup(lp+1);
			else {
				purplelogfn = (char *) malloc(strlen(xgetenv("XYMONHOME"))+1+strlen(lp+1)+1);
				sprintf(purplelogfn, "%s/%s", xgetenv("XYMONHOME"), (lp+1));
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
		else if ( argnmatch(argv[i], "--criticallog=") || (strcmp(argv[i], "--criticallog") == 0) || 
			  argnmatch(argv[i], "--nklog=") || (strcmp(argv[i], "--nklog") == 0) ){
			char *lp = strchr(argv[i], '=');
			if (lp) {
				logcritstatus = strdup(lp+1);
			}
			else logcritstatus = "critical";
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
			printf("xymongen version %s\n", VERSION);
			printf("\n");
			exit(0);
		}

		else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-?") == 0)) {
			printf("xymongen for Xymon version %s\n\n", VERSION);
			printf("Usage: %s [options] [WebpageDirectory]\n", argv[0]);
			printf("Options:\n");
			printf("    --ignorecolumns=test[,test] : Completely ignore these columns\n");
			printf("    --critical-reds-only        : Only show red statuses on the Critical page\n");
			printf("    --nongreen-ignorecolumns=test[,test]: Ignore these columns for the non-green page\n");
			printf("    --nongreen-ignorepurples         : Ignore all-purple hosts on non-green page\n");
			printf("    --includecolumns=test[,test]: Always include these columns on non-green page\n");
		        printf("    --max-eventcount=N          : Max number of events to include in eventlog\n");
		        printf("    --max-eventtime=N           : Show events that occurred within the last N minutes\n");
			printf("    --eventignore=test[,test]   : Columns to ignore in non-green event-log display\n");
			printf("    --no-eventlog               : Do not generate the non-green eventlog display\n");
			printf("    --no-acklog                 : Do not generate the non-green ack-log display\n");
			printf("    --no-pages                  : Generate only the nongreen and critical pages\n");
			printf("    --docurl=documentation-URL  : Hostnames link to a general (dynamic) web page for docs\n");
			printf("    --no-doc-window             : Open doc-links in same window\n");
			printf("    --htmlextension=.EXT        : Sets filename extension for generated file (default: .html\n");
			printf("    --report[=COLUMNNAME]       : Send a status report about the running of xymongen\n");
			printf("    --reportopts=ST:END:DYN:STL : Run in Xymon Reporting mode\n");
			printf("    --csv=FILENAME              : For Xymon Reporting, output CSV file\n");
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
			printf("    --wml[=test1,test2,...]     : Generate a small (All nongreen-style) WML page\n");
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
		printf("Command: xymongen");
		for (i=1; (i<argc); i++) printf(" '%s'", argv[i]);
		printf("\n");
		printf("Environment HOSTSCFG='%s'\n", textornull(xgetenv("HOSTSCFG")));
		printf("\n");
	}

	add_timestamp("Startup");

	/* Check that all needed environment vars are defined */
	envcheck(reqenv);

	/* Catch a SEGV fault */
	setup_signalhandler("xymongen");

	/* Set umask to 0022 so that the generated HTML pages have world-read access */
	umask(0022);

	if (pagedir == NULL) {
		if (xgetenv("XYMONWWWDIR")) {
			pagedir = strdup(xgetenv("XYMONWWWDIR"));
		}
		else {
			pagedir = (char *) malloc(strlen(xgetenv("XYMONHOME"))+5);
			sprintf(pagedir, "%s/www", xgetenv("XYMONHOME"));
		}
	}

	if (xgetenv("XYMONHTACCESS")) xymonhtaccess = strdup(xgetenv("XYMONHTACCESS"));
	if (xgetenv("XYMONPAGEHTACCESS")) xymonpagehtaccess = strdup(xgetenv("XYMONPAGEHTACCESS"));
	if (xgetenv("XYMONSUBPAGEHTACCESS")) xymonsubpagehtaccess = strdup(xgetenv("XYMONSUBPAGEHTACCESS"));

	/*
	 * When doing embedded- or snapshot-pages, dont build the WML/RSS pages.
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
	pagehead = load_layout(pageset);
	add_timestamp("Load hosts.cfg done");

	if (!embedded) {
		/* Remove old acknowledgements */
		delete_old_acks();
		add_timestamp("ACK removal done");
	}

	statehead = load_state(&dispsums);
	if (statehead == NULL) {
		errprintf("Failed to load current Xymon status, aborting page-update\n");
		return 0;
	}

	if (embedded || snapshot) dispsums = NULL;
	add_timestamp("Load STATE done");

	/* Calculate colors of hosts and pages */
	calc_hostcolors(nongreenignorecolumns);
	calc_pagecolors(pagehead);

	/* Topmost page (background color for xymon.html) */
	for (p=pagehead; (p); p = p->next) {
		if (p->color > pagehead->color) pagehead->color = p->color;
	}
	xymon_color = pagehead->color;

	if (xgetenv("SUMMARY_SET_BKG") && (strcmp(xgetenv("SUMMARY_SET_BKG"), "TRUE") == 0)) {
		/*
		 * Displayed summaries affect the Xymon page only, 
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

	/* The main page - xymon.html and pages/subpages thereunder */
	add_timestamp("Xymon pagegen start");
	if (reportstart && csvfile) {
		csv_availability(csvfile, csvdelim);
	}
	if (do_normal) {
		do_page_with_subs(pagehead, dispsums);
	}
	add_timestamp("Xymon pagegen done");

	if (reportstart) {
		/* Reports end here */
		return 0;
	}

	/* The full summary page - nongreen.html */
	if (do_nongreen) {
		nongreen_color = do_nongreen_page(nssidebarfilename, PAGE_NONGREEN);
		add_timestamp("Non-green page generation done");
	}

	/* Reduced summary (alerts) page - critical.html */
	critical_color = do_nongreen_page(NULL, PAGE_CRITICAL);
	add_timestamp("Critical page generation done");

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
		long tasksleep = (xgetenv("TASKSLEEP") ? atol(xgetenv("TASKSLEEP")) : 300);
		int color;

		/* Go yellow if it runs for too long */
		if (total_runtime() > tasksleep) {
			errprintf("WARNING: Runtime %ld longer than TASKSLEEP (%ld)\n", total_runtime(), tasksleep);
		}
		color = (errbuf ? COL_YELLOW : COL_GREEN);

		combo_start();
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n\n", xgetenv("MACHINE"), egocolumn, colorname(color), timestamp);
		addtostatus(msgline);

		sprintf(msgline, "xymongen for Xymon version %s\n", VERSION);
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

