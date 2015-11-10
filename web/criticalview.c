/*----------------------------------------------------------------------------*/
/* Xymon CGI for generating the Xymon Critical Systems page                   */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#include "libxymon.h"

typedef struct hstatus_t {
	char *hostname;
	char *testname;
	char *key;
	int color;
	time_t lastchange, logtime, validtime, acktime;
	char *ackedby, *ackmsg;
	critconf_t *config;
} hstatus_t;

static void * *rbstate = NULL;
static void * *hostsonpage = NULL;
static int treecount = 0;
static time_t oldlimit = 3600;
static int critacklevel = 1;
static int usetooltips = 0;
static time_t maxage = INT_MAX;
static time_t pagecolor = COL_GREEN;

void errormsg(char *s)
{
	fprintf(stderr, "%s\n", s);
}


static char *boardmaster = NULL;

static int getboard(int mincolor)
{
	char msg[1024];
	int i;
	sendreturn_t *sres;
	int xymondresult;


	if (!boardmaster) {
		sprintf(msg, "xymondboard acklevel=%d fields=hostname,testname,color,lastchange,logtime,validtime,acklist color=%s", critacklevel,colorname(mincolor));
		for (i=mincolor+1; (i < COL_COUNT); i++) sprintf(msg+strlen(msg), ",%s", colorname(i));

		sres = newsendreturnbuf(1, NULL);
		xymondresult = sendmessage(msg, NULL, XYMON_TIMEOUT, sres);
		if (xymondresult != XYMONSEND_OK) {
			boardmaster = "";
			freesendreturnbuf(sres);
			errormsg("Unable to fetch current status\n");
			return 1;
		}
		else {
			boardmaster = getsendreturnstr(sres, 1);
			freesendreturnbuf(sres);
		}
	}

	return 0;
}

int loadstatus(int maxprio, time_t maxage, int mincolor, int wantacked)
{
	char *board, *bol, *eol;
	time_t now;

	/* 
	 * We leak memory by dup'ing this and not freeing it. 
	 * But we cannot free it, because the tree holding the data
	 * for later printing contains pointers into this string buffer.
	 */
	board = strdup(boardmaster);

	now = getcurrenttime(NULL);
	treecount++;
	if (treecount == 1) {
		rbstate = malloc(sizeof(void *));
		hostsonpage = malloc(sizeof(void *));
	}
	else {
		rbstate = realloc(rbstate, (treecount) * sizeof(void *));
		hostsonpage = realloc(hostsonpage, (treecount) * sizeof(void *));
	}
	rbstate[treecount-1] = xtreeNew(strcasecmp);
	hostsonpage[treecount-1] = xtreeNew(strcasecmp);

	bol = board;
	while (bol && (*bol)) {
		char *endkey;
		eol = strchr(bol, '\n'); if (eol) *eol = '\0';

		/* Find the config entry */
		endkey = strchr(bol, '|'); if (endkey) endkey = strchr(endkey+1, '|'); 
		if (endkey) {
			critconf_t *cfg;
			char *ackstr, *ackrtimestr, *ackvtimestr, *acklevelstr, *ackbystr, *ackmsgstr;

			*endkey = '\0';
			cfg = get_critconfig(bol, CRITCONF_TIMEFILTER, NULL);
			*endkey = '|';

			if (cfg) {
				hstatus_t *newitem = (hstatus_t *)calloc(1, sizeof(hstatus_t));
				newitem->config     = cfg;
				newitem->hostname   = gettok(bol, "|");
				newitem->testname   = gettok(NULL, "|");
				newitem->color      = parse_color(gettok(NULL, "|"));
				newitem->lastchange = atoi(gettok(NULL, "|"));
				newitem->logtime    = atoi(gettok(NULL, "|"));
				newitem->validtime  = atoi(gettok(NULL, "|"));
				ackstr              = gettok(NULL, "|");
				ackrtimestr = ackvtimestr = acklevelstr = ackbystr = ackmsgstr = NULL;

				if (ackstr) {
					nldecode(ackstr);
					ackrtimestr = strtok(ackstr, ":");
					if (ackrtimestr) ackvtimestr = strtok(NULL, ":");
					if (ackvtimestr) acklevelstr = strtok(NULL, ":");
					if (acklevelstr) ackbystr = strtok(NULL, ":");
					if (ackbystr)    ackmsgstr = strtok(NULL, ":");
				}

				if ( (hostinfo(newitem->hostname) == NULL)  ||
				     ((newitem->config->priority > maxprio)  && (newitem->config->priority != 99)) ||
				     ((now - newitem->lastchange) > maxage) ||
				     (newitem->color < mincolor)            ||
				     (ackmsgstr && !wantacked)              ) {
					xfree(newitem);
				}
				else {
					if (ackvtimestr && ackbystr && ackmsgstr) {
						newitem->acktime = atoi(ackvtimestr);
						newitem->ackedby = strdup(ackbystr);
						newitem->ackmsg  = strdup(ackmsgstr);
					}

					newitem->key = (char *)malloc(strlen(newitem->hostname) + strlen(newitem->testname) + 2);
					sprintf(newitem->key, "%s|%s", newitem->hostname, newitem->testname);
					xtreeAdd(rbstate[treecount-1], newitem->key, newitem);
				}
			}
		}

		bol = (eol ? (eol+1) : NULL);
	}

	return 0;
}


void * columnlist(void * statetree)
{
	void * rbcolumns;
	xtreePos_t hhandle;

	rbcolumns = xtreeNew(strcasecmp);
	for (hhandle = xtreeFirst(statetree); (hhandle != xtreeEnd(statetree)); hhandle = xtreeNext(statetree, hhandle)) {
		hstatus_t *itm;

		itm = (hstatus_t *)xtreeData(statetree, hhandle);
		xtreeAdd(rbcolumns, itm->testname, NULL);
	}

	return rbcolumns;
}

void print_colheaders(FILE *output, void * rbcolumns)
{
	int colcount;
	xtreePos_t colhandle;

	colcount = 1;	/* Remember the hostname column */

	/* Group column headings */
	fprintf(output, "<TR>");
	fprintf(output, "<TD ROWSPAN=2>&nbsp;</TD>\n");	/* For the prio column - in both row headers+dash rows */
	fprintf(output, "<TD ROWSPAN=2>&nbsp;</TD>\n");	/* For the host column - in both row headers+dash rows */
	for (colhandle = xtreeFirst(rbcolumns); (colhandle != xtreeEnd(rbcolumns)); colhandle = xtreeNext(rbcolumns, colhandle)) {
		char *colname;

		colname = (char *)xtreeKey(rbcolumns, colhandle);
		colcount++;

		fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
		fprintf(output, " <A HREF=\"%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n",
			columnlink(colname), xgetenv("XYMONPAGECOLFONT"), colname);
	}
	fprintf(output, "</TR>\n");
	fprintf(output, "<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n\n", colcount);
}

void print_hoststatus(FILE *output, hstatus_t *itm, void * statetree, void * columns, int prio, int firsthost, int firsthostever)
{
	char *key;
	time_t now;
	xtreePos_t colhandle;

	now = getcurrenttime(NULL);

	fprintf(output, "<TR>\n");

	/* Print the priority */
	fprintf(output, "<TD ALIGN=LEFT VALIGN=TOP WIDTH=10%% NOWRAP>");
	if (firsthost) 
		if (prio == 99) {
			if (firsthostever)
				/* Only non-prioritised hosts, so just drop that text */
				fprintf(output, "&nbsp;");
			else
				fprintf(output, "<FONT %s>No priority</FONT>", xgetenv("XYMONPAGEROWFONT"));
		}
		else {
			fprintf(output, "<FONT %s>PRIO %d</FONT>", xgetenv("XYMONPAGEROWFONT"), prio);
		}

	else 
		fprintf(output, "&nbsp;");
	fprintf(output, "</TD>\n");

	/* Print the hostname with a link to the critical systems info page */
	fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", hostnamehtml(itm->hostname, NULL, usetooltips));

	key = (char *)malloc(1024);
	for (colhandle = xtreeFirst(columns); (colhandle != xtreeEnd(columns)); colhandle = xtreeNext(columns, colhandle)) {
		char *colname;
		xtreePos_t sthandle;

		fprintf(output, "<TD ALIGN=CENTER>");

		colname = (char *)xtreeKey(columns, colhandle);
		key = (char *)realloc(key, 2 + strlen(itm->hostname) + strlen(colname));
		sprintf(key, "%s|%s", itm->hostname, colname);
		sthandle = xtreeFind(statetree, key);
		if (sthandle == xtreeEnd(statetree)) {
			fprintf(output, "-");
		}
		else {
			hstatus_t *column;
			char *htmlalttag;
			char *htmlackstr;

			column = (hstatus_t *)xtreeData(statetree, sthandle);
			if (column->config->priority != prio) 
				fprintf(output, "-");
			else {
				time_t age = now - column->lastchange;
				char *htmlgroupstr;
				char *htmlextrastr;

				htmlalttag = alttag(colname, column->color, 0, 1, agestring(age));
				htmlackstr = (column->ackmsg ? column->ackmsg : "");
				htmlgroupstr = strdup(urlencode(column->config->ttgroup ? column->config->ttgroup : ""));
				htmlextrastr = strdup(urlencode(column->config->ttextra ? column->config->ttextra : ""));
				fprintf(output, "<A HREF=\"%s&amp;NKPRIO=%d&amp;NKTTGROUP=%s&amp;NKTTEXTRA=%s\">",
					hostsvcurl(itm->hostname, colname, 1),
					prio, 
					htmlgroupstr, htmlextrastr);
				fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s %s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
					xgetenv("XYMONSKIN"), 
					dotgiffilename(column->color, (column->acktime > 0), (age > oldlimit)),
					colorname(column->color), htmlalttag, htmlackstr,
					xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
				xfree(htmlgroupstr);
				xfree(htmlextrastr);
			}
		}

		fprintf(output, "</TD>\n");
	}
	xfree(key);

	fprintf(output, "</TR>\n");
}


void print_oneprio(FILE *output, void * statetree, void * hoptree, void * rbcolumns, int prio)
{
	xtreePos_t hhandle;
	static int firsthostever = 1;
	int firsthostthisprio = 1;
	char *curhost = "";

	/* Then output each host and their column status */
	for (hhandle = xtreeFirst(statetree); (hhandle != xtreeEnd(statetree)); hhandle = xtreeNext(statetree, hhandle)) {
		hstatus_t *itm;

		itm = (hstatus_t *)xtreeData(statetree, hhandle);

		if (itm->config->priority != prio) continue;
		if (strcmp(curhost, itm->hostname) == 0) continue;

		/* New host */
		curhost = itm->hostname;
		print_hoststatus(output, itm, statetree, rbcolumns, prio, firsthostthisprio, firsthostever);
		xtreeAdd(hoptree, itm->hostname, itm);
		firsthostthisprio = 0;
	}

	/* If we did output any hosts, make some room for the next priority */
	if (!firsthostthisprio) fprintf(output, "<TR><TD>&nbsp;</TD></TR>\n");
}


static int evcount = 0;
static void * evhopfilter;

static int ev_included(char *hostname)
{
	/* Callback function for filtering eventlog-hosts */
	return (xtreeFind(evhopfilter, hostname) == xtreeEnd(evhopfilter)) ? 0 : 1;
}


void generate_critpage(void * statetree, void * hoptree, FILE *output, char *header, char *footer, int color, int maxprio)
{
        headfoot(output, header, "", "header", pagecolor);	/* Use PAGE color here, not the part color */
        fprintf(output, "<center>\n");

        if (color != COL_GREEN) {
		void * rbcolumns;
		int prio;

		rbcolumns = columnlist(statetree);

		fprintf(output, "<TABLE BORDER=0 CELLPADDING=4 SUMMARY=\"Critical status display\">\n");
		print_colheaders(output, rbcolumns);

		for (prio = 1; (prio <= maxprio); prio++) {
			print_oneprio(output, statetree, hoptree, rbcolumns, prio);
		}

		fprintf(output, "</TABLE>\n");
		xtreeDestroy(rbcolumns);
        }
        else {
                /* "All Monitored Systems OK */
		fprintf(output, "%s", xgetenv("XYMONALLOKTEXT"));
        }

	if (evcount > 0) {
		/* Include the eventlog */
		evhopfilter = hoptree;
		do_eventlog(output, evcount, maxage/60, 
			    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0,
			    ev_included,
			    NULL, NULL, NULL, XYMON_COUNT_NONE, XYMON_S_NONE, NULL);
		fprintf(output, "<br><br><br>\n");
	}

        fprintf(output, "</center>\n");

        headfoot(output, footer, "", "footer", color);
}


static int maxprio = 3;
static int mincolor = COL_YELLOW;
static int wantacked = 0;

static void selectenv(char *name, char *val)
{
	char *env;
	char *p;
	int envlen;

	envlen = 20;
	envlen += strlen(htmlquoted(name));
	envlen += strlen(htmlquoted(val));

	env = (char *)malloc(envlen);
	sprintf(env, "SELECT_%s", htmlquoted(name));
	sprintf(env+strlen(env), "_%s=SELECTED", htmlquoted(val));

	for (p=env; (*p); p++) *p = toupper((int)*p);
	putenv(env);
}

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;
	int havemaxprio=0, havemaxage=0, havemincolor=0, havewantacked=0, haveevcount=0;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "MAXPRIO") == 0) {
			selectenv(cwalk->name, cwalk->value);
			maxprio = atoi(cwalk->value);
			havemaxprio = 1;
		}
		else if (strcasecmp(cwalk->name, "MAXAGE") == 0) {
			selectenv(cwalk->name, cwalk->value);
			maxage = 60*atoi(cwalk->value);
			havemaxage = 1;
		}
		else if (strcasecmp(cwalk->name, "MINCOLOR") == 0) {
			selectenv(cwalk->name, cwalk->value);
			mincolor = parse_color(cwalk->value);
			havemincolor = 1;
		}
		else if (strcasecmp(cwalk->name, "OLDLIMIT") == 0) {
			selectenv(cwalk->name, cwalk->value);
			oldlimit = 60*atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "WANTACKED") == 0) {
			selectenv(cwalk->name, cwalk->value);
			wantacked = (strcasecmp(cwalk->value, "yes") == 0);
			havewantacked = 1;
		}
		else if (strcasecmp(cwalk->name, "EVCOUNT") == 0) {
			selectenv(cwalk->name, cwalk->value);
			evcount = atoi(cwalk->value);
			haveevcount = 1;
		}

		cwalk = cwalk->next;
	}

	if (!havemaxprio)   selectenv("MAXPRIO", "3");
	if (!havemaxage)    selectenv("MAXAGE", "525600");
	if (!havemincolor)  selectenv("MINCOLOR", "yellow");
	if (!havewantacked) selectenv("WANTACKED", "no");
	if (!haveevcount)   selectenv("EVCOUNT", "0");
}


int main(int argc, char *argv[])
{
	int argi;
	char **critconfig = NULL;
	int cccount = 0;
	char *hffile = "critical";

	critconfig = (char **)calloc(1, sizeof(char *));

	libxymon_init(argv[0]);
	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--tooltips") == 0) {
			usetooltips = 1;
		}
		else if (argnmatch(argv[argi], "--acklevel=")) {
			char *p = strchr(argv[argi], '=');
			critacklevel = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');

			critconfig[cccount] = strdup(p+1);
			cccount++;
			critconfig = (char **)realloc(critconfig, (1 + cccount)*sizeof(char *));
			critconfig[cccount] = NULL;
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
		else if (argnmatch(argv[argi], "--hffile=")) {
			char *p = strchr(argv[argi], '=');
			hffile = strdup(p+1);
		}
	}

	if (!critconfig[0]) {
		critconfig = (char **)realloc(critconfig, 2*sizeof(char *));
		critconfig[0] = (char *)malloc(strlen(xgetenv("XYMONHOME")) + strlen(DEFAULT_CRITCONFIGFN) + 2);
		sprintf(critconfig[0], "%s/%s", xgetenv("XYMONHOME"), DEFAULT_CRITCONFIGFN);
		critconfig[1] = NULL;
	}

	redirect_cgilog(programname);

	setdocurl(hostsvcurl("%s", xgetenv("INFOCOLUMN"), 1));

	parse_query();
	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
	load_all_links();
	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	use_recentgifs = 1;

	if (getboard(mincolor) == 0) {
		int i;
		char *oneconfig, *onename;
		int *partcolor = NULL, *partprio = NULL;
		xtreePos_t hhandle;

		for (i=0; (critconfig[i]); i++) {
			oneconfig = strchr(critconfig[i], ':');
			load_critconfig(oneconfig ? oneconfig+1 : critconfig[i]);
			loadstatus(maxprio, maxage, mincolor, wantacked);

			/* Determine background color and max. priority */
			if (i == 0) {
				partcolor = (int *)malloc(sizeof(int));
				partprio = (int *)malloc(sizeof(int));
			}
			else {
				partcolor = (int *)realloc(partcolor, (i+1)*sizeof(int));
				partprio = (int *)realloc(partprio, (i+1)*sizeof(int));
			}
			partcolor[i] = COL_GREEN;
			partprio[i] = 0;

			for (hhandle = xtreeFirst(rbstate[i]); (hhandle != xtreeEnd(rbstate[i])); hhandle = xtreeNext(rbstate[i], hhandle)) {
				hstatus_t *itm;

				itm = (hstatus_t *)xtreeData(rbstate[i], hhandle);

				if (itm->color > partcolor[i]) partcolor[i] = itm->color;
				if (itm->config->priority > partprio[i]) partprio[i] = itm->config->priority;
			}

			if (partcolor[i] > pagecolor) pagecolor = partcolor[i];
		}

		for (i=0; (critconfig[i]); i++) {
			oneconfig = strchr(critconfig[i], ':'); 
			if (oneconfig) {
				*oneconfig = '\0';
				oneconfig++;
				onename = (char *)malloc(strlen("DIVIDERTEXT=") + strlen(critconfig[i]) + 1);
				sprintf(onename, "DIVIDERTEXT=%s", critconfig[i]);
				putenv(onename);
			}
			else {
				oneconfig = critconfig[i];
				putenv("DIVIDERTEXT=");
			}

			generate_critpage(rbstate[i], hostsonpage[i], stdout, 
					  (i == 0) ? (critconfig[1] ? "critmulti" : hffile) : "divider", 
					  (critconfig[i+1] == NULL) ? hffile : "divider",
					  partcolor[i], partprio[i]);
		}
	}
	else {
		fprintf(stdout, "Cannot load Xymon status\n");
	}

	return 0;
}

