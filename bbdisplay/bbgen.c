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

static char rcsid[] = "$Id: bbgen.c,v 1.167 2004-01-25 22:21:12 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include "bbgen.h"
#include "util.h"
#include "loadhosts.h"
#include "loaddata.h"
#include "process.h"
#include "pagegen.h"
#include "larrdgen.h"
#include "infogen.h"
#include "alert.h"
#include "debug.h"
#include "wmlgen.h"
#include "bb-replog.h"
#include "sendmsg.h"
#include "rssgen.h"

/* Global vars */
bbgen_page_t	*pagehead = NULL;			/* Head of page list */
link_t  	*linkhead = NULL;			/* Head of links list */
hostlist_t	*hosthead = NULL;			/* Head of hosts list */
state_t		*statehead = NULL;			/* Head of list of all state entries */
summary_t	*sumhead = NULL;			/* Summaries we send out */
dispsummary_t	*dispsums = NULL;			/* Summaries we received and display */
int		bb_color, bb2_color, bbnk_color;	/* Top-level page colors */
int		fqdn = 1;				/* BB FQDN setting */

time_t		reportstart = 0;
time_t		reportend = 0;
double		reportwarnlevel = 97.0;
double		reportgreenlevel = 99.995;
int		reportstyle = STYLE_CRIT;
int		dynamicreport = 1;

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
	char		*rrddir;
	bbgen_page_t 	*p;
	dispsummary_t	*s;
	int		i;
	int		pagegenstat;
	char		*pageset = NULL;
	char 		bb2filename[MAX_PATH];
	char 		bbnkfilename[MAX_PATH];
	int             larrd043 = 0;				/* Set to use LARRD 0.43 disk displays */
	char		*egocolumn = NULL;
	int		embedded = 0;

	/* Setup standard header+footer (might be modified by option pageset) */
	select_headers_and_footers("bb");

	bb_color = bb2_color = bbnk_color = -1;
	pagedir = rrddir = NULL;
	init_timestamp();
	fqdn = get_fqdn();

	/* Setup values from env. vars that may be overridden via commandline options */
	if (getenv("MKBB2COLREPEAT")) {
		int i = atoi(getenv("MKBB2COLREPEAT"));

		if (i > 0) maxrowsbeforeheading = i;
	}

	for (i = 1; (i < argc); i++) {
		if (strcmp(argv[i], "--nopurple") == 0) {
			enable_purpleupd = 0;
		}
		else if (argnmatch(argv[i], "--purplelifetime=")) {
			char *lp = strchr(argv[i], '=');

			purpledelay = atoi(lp+1);
			if (purpledelay < 0) purpledelay=0;
		}

		else if (argnmatch(argv[i], "--ignorecolumns=")) {
			char *lp = strchr(argv[i], '=');
			ignorecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(ignorecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--bb2-ignorecolumns=")) {
			char *lp = strchr(argv[i], '=');
			bb2ignorecolumns = (char *) malloc(strlen(lp)+2);
			sprintf(bb2ignorecolumns, ",%s,", (lp+1));
		}
		else if (argnmatch(argv[i], "--bb2-ignorepurples")) {
			bb2includepurples = 0;
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
			documentationurl = (char *)malloc(strlen(getenv("CGIBINURL"))+strlen(lp+1)+2);
			sprintf(documentationurl, "%s/%s", getenv("CGIBINURL"), lp+1);
		}
		else if (argnmatch(argv[i], "--docurl=")) {
			char *lp = strchr(argv[i], '=');
			documentationurl = malcop(lp+1);
		}
		else if (argnmatch(argv[i], "--no-doc-window")) {
			doctargetspec = "";
		}
		else if (argnmatch(argv[i], "--htmlextension=")) {
			char *lp = strchr(argv[i], '=');
			htmlextension = malcop(lp+1);
		}
		else if (argnmatch(argv[i], "--htaccess")) {
			char *lp = strchr(argv[i], '=');
			if (lp) htaccess = malcop(lp+1);
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

			if (strlen(lp+1) > 0) nssidebarfilename = malcop(lp+1);
			else errprintf("--nstab requires a filename\n");
		}
		else if (argnmatch(argv[i], "--rss=")) {
			char *lp = strchr(argv[i], '=');

			if (strlen(lp+1) > 0) rssfilename = malcop(lp+1);
			else errprintf("--rss requires a filename\n");
		}
		else if (argnmatch(argv[i], "--rssversion=")) {
			char *lp = strchr(argv[i], '=');

			rssversion = malcop(lp+1);
		}
		else if (argnmatch(argv[i], "--reportopts=")) {
			char style[MAX_LINE_LEN];

			int count = sscanf(argv[i], "--reportopts=%u:%u:%d:%s", 
					   (unsigned int *)&reportstart, (unsigned int *)&reportend, 
					   &dynamicreport, style);

			if (count < 1) reportstart = 788918400;	/* 01-Jan-1995 00:00 GMT */
			if (count < 2) reportend = time(NULL);
			if (count < 3) dynamicreport = 1;
			if (count == 4) {
				if (strcmp(style, stylenames[STYLE_CRIT]) == 0) reportstyle = STYLE_CRIT;
				else if (strcmp(style, stylenames[STYLE_NONGR]) == 0) reportstyle = STYLE_NONGR;
				else reportstyle = STYLE_OTHER;
			}

			if (reportstart < 788918400) reportstart = 788918400;
			if (reportend > time(NULL)) reportend = time(NULL);

			if (getenv("BBREPWARN")) reportwarnlevel = atof(getenv("BBREPWARN"));
			if (getenv("BBREPGREEN")) reportgreenlevel = atof(getenv("BBREPGREEN"));

			if ((reportwarnlevel < 0.0) || (reportwarnlevel > 100.0)) reportwarnlevel = 97.0;
			if ((reportgreenlevel < 0.0) || (reportgreenlevel > 100.0)) reportgreenlevel = 99.995;

			select_headers_and_footers("bbrep");
			sethostenv_report(reportstart, reportend, reportwarnlevel, reportgreenlevel);
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
		else if (strcmp(argv[i], "--sort-group-only-items") == 0) {
			sort_grouponly_items = 1;
		}
		else if (argnmatch(argv[i], "--page-title=")) {
			char *lp = strchr(argv[i], '=');

			defaultpagetitle = malcop(lp+1);
		}
		else if (argnmatch(argv[i], "--dialupskin=")) {
			char *lp = strchr(argv[i], '=');

			dialupskin = malcop(lp+1);
		}
		else if (argnmatch(argv[i], "--reverseskin=")) {
			char *lp = strchr(argv[i], '=');

			reverseskin = malcop(lp+1);
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
		else if (strcmp(argv[i], "--no-acklog") == 0) {
			bb2acklog = 0;
		}
		else if (strcmp(argv[i], "--unpatched-bbd") == 0) {
			unpatched_bbd = 1;
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

		else if (argnmatch(argv[i], "--infoupdate=")) {
			char *lp = strchr(argv[i], '=');

			info_update_interval = atoi(lp+1);
			if (info_update_interval <= 0) enable_infogen=0;
			else enable_infogen = 1;
		}
		else if ((strcmp(argv[i], "--info") == 0) || argnmatch(argv[i], "--info=")) {
			/* "--info" just enable info page generation */
			/* "--info=xxx" does that, and redefines the info column name */
			char *lp = strchr(argv[i], '=');

			enable_infogen=1;
			if (lp) infocol = malcop(lp+1);
		}

		else if (argnmatch(argv[i], "--larrdupdate=")) {
			char *lp = strchr(argv[i], '=');

			larrd_update_interval = atoi(lp+1);
			if (larrd_update_interval <= 0) enable_larrdgen=0;
			else enable_larrdgen = 1;
		}
		else if (argnmatch(argv[i], "--larrdgraphs=")) {
			char *lp = strchr(argv[i], '=');

			enable_larrdgen=1;
			if (lp) larrdgraphs_default = malcop(lp+1);
		}
		else if (argnmatch(argv[i], "--larrd043=") || (strcmp(argv[i], "--larrd043") == 0)) {
			/* "--larrd" just enable larrd page generation */
			/* "--larrd=xxx" does that, and redefines the larrd column name */
			char *lp = strchr(argv[i], '=');

			enable_larrdgen=1;
			if (lp) larrdcol = malcop(lp+1); else larrdcol = "trends";
			larrd043 = 1;
		}
		else if (argnmatch(argv[i], "--larrd=") || (strcmp(argv[i], "--larrd") == 0)) {
			/* "--larrd" just enable larrd page generation */
			/* "--larrd=xxx" does that, and redefines the larrd column name */
			char *lp = strchr(argv[i], '=');

			enable_larrdgen=1;
			if (lp) larrdcol = malcop(lp+1); else larrdcol = "larrd";
		}
		else if (argnmatch(argv[i], "--rrddir=")) {
			char *lp = strchr(argv[i], '=');
			rrddir = malcop(lp+1);
		}
		else if (strcmp(argv[i], "--log-nohost-rrds") == 0) {
			log_nohost_rrds=1;
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
			pageset = malcop(lp+1);
		}
		else if (argnmatch(argv[i], "--template=")) {
			char *lp = strchr(argv[i], '=');
			lp++;
			select_headers_and_footers(lp);
		}

		else if (argnmatch(argv[i], "--purplelog=")) {
			char *lp = strchr(argv[i], '=');
			if (*(lp+1) == '/') purplelogfn = malcop(lp+1);
			else {
				purplelogfn = (char *) malloc(strlen(getenv("BBHOME"))+1+strlen(lp+1)+1);
				sprintf(purplelogfn, "%s/%s", getenv("BBHOME"), (lp+1));
			}
		}
		else if (argnmatch(argv[i], "--report=") || (strcmp(argv[i], "--report") == 0)) {
			char *lp = strchr(argv[i], '=');
			if (lp) {
				egocolumn = malcop(lp+1);
			}
			else egocolumn = "bbgen";
			timing = 1;
		}
		else if (argnmatch(argv[i], "--nklog=") || (strcmp(argv[i], "--nklog") == 0)) {
			char *lp = strchr(argv[i], '=');
			if (lp) {
				lognkstatus = malcop(lp+1);
			}
			else lognkstatus = "nk";
		}
		else if (strcmp(argv[i], "--timing") == 0) {
			timing = 1;
		}
		else if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[i], "--version") == 0) {
			printf("bbgen version %s\n", VERSION);
			printf("Compile settings: MAXMSG=%d, BBDPORTNUMBER=%d", MAXMSG, BBDPORTNUMBER);
#ifdef DEBUG
			printf(", DEBUG");
#endif
			printf("\n");
			exit(0);
		}

		else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-?") == 0)) {
			printf("bbgen version %s\n\n", VERSION);
			printf("Usage: %s [options] [WebpageDirectory]\n", argv[0]);
			printf("Options:\n");
			printf("    --nopurple                  : Disable purple status-updates\n");
			printf("    --purplelifetime=N          : Purple messages have a lifetime of N minutes\n");
			printf("    --ignorecolumns=test[,test] : Completely ignore these columns\n");
			printf("    --bb2-ignorecolumns=test[,test]: Ignore these columns for the BB2 page\n");
			printf("    --bb2-ignorepurples         : Ignore all-purple hosts on BB2 page\n");
			printf("    --includecolumns=test[,test]: Always include these columns on BB2 page\n");
		        printf("    --max-eventcount=N          : Max number of events to include in eventlog\n");
		        printf("    --max-eventtime=N           : Show events that occurred within the last N minutes\n");
			printf("    --eventignore=test[,test]   : Columns to ignore in bb2 event-log display\n");
			printf("    --no-eventlog               : Do not generate the bb2 eventlog display\n");
			printf("    --no-acklog                 : Do not generate the bb2 ack-log display\n");
			printf("    --docurl=documentation-URL  : Hostnames link to a general (dynamic) web page for docs\n");
			printf("    --no-doc-window             : Open doc-links in same window\n");
			printf("    --htmlextension=.EXT        : Sets filename extension for generated file (default: .html\n");
			printf("    --report[=COLUMNNAME]       : Send a status report about the running of bbgen\n");
			printf("    --reportopts=ST:END:DYN:STL : Run in BB Reporting mode\n");
			printf("    --snapshot=TIME             : Snapshot mode\n");
			printf("\nPage layout options:\n");
			printf("    --pages-last                : Put page- and subpage-links after hosts (as BB does)\n");
			printf("    --pages-first               : Put page- and subpage-links before hosts (default)\n");
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
			printf("\nInfo column options:\n");
			printf("    --info[=INFOCOLUMN]         : Generate INFO data in column INFOCOLUMN\n");
			printf("    --infoupdate=N              : time between updates of INFO column pages in seconds\n");
			printf("\nLARRD support options:\n");
			printf("    --larrd[=LARRDCOLUMN]       : LARRD data in column LARRDCOLUMN, and handle larrd-html for LARRD 0.42\n");
			printf("    --larrd043[=LARRDCOLUMN]    : LARRD data in column LARRDCOLUMN, and handle larrd-html for LARRD 0.43\n");
			printf("    --larrdgraphs=GRAPHSPEC     : Set a default value for the LARRD: bb-hosts tag\n");
			printf("    --larrdupdate=N             : time between updates of LARRD pages in seconds\n");
			printf("    --rrddir=RRD-directory      : Directory for LARRD RRD files\n");
			printf("\nAlternate pageset generation support:\n");
			printf("    --pageset=SETNAME           : Generate non-standard pageset with tag SETNAME\n");
			printf("    --template=TEMPLATE         : template for header and footer files\n");
			printf("\nAlternate output formats:\n");
			printf("    --wml[=test1,test2,...]     : Generate a small (bb2-style) WML page\n");
			printf("    --nstab=FILENAME            : Generate a Netscape Sidebar feed\n");
			printf("    --rss=FILENME               : Generate a RSS/RDF feed of alerts\n");
			printf("    --rssversion={0.91|0.92|1.0|2.0} : Specify RSS/RDF version (default: 0.91)\n");
			printf("\nDebugging/troubleshooting options:\n");
			printf("    --timing                    : Collect timing information\n");
			printf("    --debug                     : Debugging information\n");
			printf("    --version                   : Show version information\n");
			printf("    --purplelog=FILENAME        : Create a log of purple hosts and tests\n");
			printf("    --unpatched-bbd             : BB has not been patched with bbd-background.patch\n");
			exit(0);
		}
		else if (argnmatch(argv[i], "-")) {
			errprintf("Unknown option : %s\n", argv[i]);
		}

		else {
			/* Last argument is pagedir */
			pagedir = malcop(argv[i]);
		}
	}

	/* In case they changed the name of our column ... */
	if (egocolumn) setup_signalhandler(egocolumn);

	if (debug) {
		int i;
		printf("Command: bbgen");
		for (i=1; (i<argc); i++) printf(" '%s'", argv[i]);
		printf("\n");
		printf("Environment BBHOSTS='%s'\n", textornull(getenv("BBHOSTS")));
		printf("\n");
	}

	add_timestamp("Startup");

	/* Check that all needed environment vars are defined */
	envcheck(reqenv);

	/* Catch a SEGV fault */
	setup_signalhandler("bbgen");

	if (pagedir == NULL) {
		if (getenv("BBWWW")) {
			pagedir = malcop(getenv("BBWWW"));
		}
		else {
			pagedir = (char *) malloc(strlen(getenv("BBHOME"))+5);
			sprintf(pagedir, "%s/www", getenv("BBHOME"));
		}
	}
	if (rrddir == NULL) {
		rrddir = (char *) malloc(strlen(getenv("BBVAR"))+5);
		sprintf(rrddir, "%s/rrd", getenv("BBVAR"));
	}

	if (getenv("BBHTACCESS")) bbhtaccess = malcop(getenv("BBHTACCESS"));
	if (getenv("BBPAGEHTACCESS")) bbpagehtaccess = malcop(getenv("BBPAGEHTACCESS"));
	if (getenv("BBSUBPAGEHTACCESS")) bbsubpagehtaccess = malcop(getenv("BBSUBPAGEHTACCESS"));

	/*
	 * When doing alternate pagesets, disable some stuff:
	 * No LARRD, no INFO, no purple updates, no WML 
	 * If we did those, we would send double purple updates, 
	 * generate wrong links for info pages etc.
	 */
	if (pageset || embedded || snapshot) enable_purpleupd = enable_larrdgen = enable_infogen = enable_wmlgen = 0;
	if (embedded) {
		egocolumn = htaccess = NULL;

		/*
		 * Need to have default SIGPIPE handling when doing embedded stuff.
		 * We are probably run from a CGI script or something similar.
		 */
		signal(SIGPIPE, SIG_DFL);
	}

	/* Load all data from the various files */
	linkhead = load_all_links();
	add_timestamp("Load links done");
	pagehead = load_bbhosts(pageset);
	add_timestamp("Load bbhosts done");

	if (!embedded) {
		/* Delete old info- and larrd-timestamp files if we have restarted */
		drop_genstatfiles();

		/* Generate the LARRD pages before loading state */
		pagegenstat = generate_larrd(rrddir, larrdcol, larrd043);
		add_timestamp("LARRD generate done");

		/* Dont generate both LARRD and info in one run */
		if (pagegenstat) pagegenstat = generate_info(infocol);
		add_timestamp("INFO generate done");

		/* Remove old acknowledgements */
		delete_old_acks();
		add_timestamp("ACK removal done");
	}

	statehead = load_state(&dispsums);
	if (embedded || snapshot) dispsums = NULL;
	add_timestamp("Load STATE done");

	/* Calculate colors of hosts and pages */
	calc_hostcolors(hosthead, bb2ignorecolumns);
	calc_pagecolors(pagehead);

	/* Topmost page (background color for bb.html) */
	for (p=pagehead; (p); p = p->next) {
		if (p->color > pagehead->color) pagehead->color = p->color;
	}
	bb_color = pagehead->color;

	if (getenv("SUMMARY_SET_BKG") && (strcmp(getenv("SUMMARY_SET_BKG"), "TRUE") == 0)) {
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
	add_timestamp("BB pagegen start");
	do_page_with_subs(pagehead, dispsums);
	add_timestamp("BB pagegen done");

	if (reportstart) {
		/* Reports end here */
		return 0;
	}

	/* The full summary page - bb2.html */
	sprintf(bb2filename, "bb2%s", htmlextension);
	bb2_color = do_bb2_page(bb2filename, PAGE_BB2);
	add_timestamp("BB2 generation done");

	/* Reduced summary (alerts) page - bbnk.html */
	sprintf(bbnkfilename, "bbnk%s", htmlextension);
	bbnk_color = do_bb2_page(bbnkfilename, PAGE_NK);
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
	do_wml_cards(pagedir);
	add_timestamp("WML generation done");

	/* Generate RSS/RDF feed */
	if (rssfilename) {
		do_rss_feed();
		add_timestamp("RSS generation done");
	}

	/* Generate Netscape sidebar feed */
	if (nssidebarfilename) {
		do_netscape_sidebar();
		add_timestamp("Netscape Sidebar generation done");
	}

	/* Need to do this before sending in our report */
	add_timestamp("Run completed");

	/* Tell about us */
	if (egocolumn) {
		char msgline[MAXMSG];
		char *timestamps;
		long bbsleep = (getenv("BBSLEEP") ? atol(getenv("BBSLEEP")) : 300);
		int color;

		/* Go yellow if it runs for too long */
		if (total_runtime() > bbsleep) {
			errprintf("WARNING: Runtime %ld longer than BBSLEEP (%ld)\n", total_runtime(), bbsleep);
		}
		color = (errbuf ? COL_YELLOW : COL_GREEN);

		combo_start();
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n\n", getenv("MACHINE"), egocolumn, colorname(color), timestamp);
		addtostatus(msgline);

		sprintf(msgline, "bbgen version %s\n", VERSION);
		addtostatus(msgline);

		sprintf(msgline, "\nStatistics:\n Hosts               : %5d\n Status messages     : %5d\n Purple messages     : %5d\n Pages               : %5d\n", 
			hostcount, statuscount, purplecount, pagecount);
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

