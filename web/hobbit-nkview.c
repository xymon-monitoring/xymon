/*----------------------------------------------------------------------------*/
/* Hobbit CGI for generating the Hobbit NK page                               */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@storner.dk>                 */
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

#include "libbbgen.h"

typedef struct hstatus_t {
	char *hostname;
	char *testname;
	char *key;
	int color;
	time_t lastchange, logtime, validtime, acktime;
	char *ackedby, *ackmsg;
	nkconf_t *config;
} hstatus_t;

static RbtHandle rbstate;
static time_t oldlimit = 3600;
static int nkacklevel = 1;

void errormsg(char *s)
{
	fprintf(stderr, "%s\n", s);
}


int loadstatus(int maxprio, time_t maxage, int mincolor, int wantacked)
{
	int hobbitdresult;
	char *board = NULL;
	char *bol, *eol;
	time_t now;
	char msg[1024];
	int i;
	sendreturn_t *sres;

	sprintf(msg, "hobbitdboard acklevel=%d fields=hostname,testname,color,lastchange,logtime,validtime,acklist color=%s", nkacklevel,colorname(mincolor));
	for (i=mincolor+1; (i < COL_COUNT); i++) sprintf(msg+strlen(msg), ",%s", colorname(i));

	sres = newsendreturnbuf(1, NULL);
	hobbitdresult = sendmessage(msg, NULL, BBTALK_TIMEOUT, sres);
	if (hobbitdresult != BB_OK) {
		freesendreturnbuf(sres);
		errormsg("Unable to fetch current status\n");
		return 1;
	}
	else {
		board = getsendreturnstr(sres, 1);
		freesendreturnbuf(sres);
	}

	now = getcurrenttime(NULL);
	rbstate = rbtNew(name_compare);

	bol = board;
	while (bol && (*bol)) {
		char *endkey;
		RbtStatus status;

		eol = strchr(bol, '\n'); if (eol) *eol = '\0';

		/* Find the config entry */
		endkey = strchr(bol, '|'); if (endkey) endkey = strchr(endkey+1, '|'); 
		if (endkey) {
			nkconf_t *cfg;
			char *ackstr, *ackrtimestr, *ackvtimestr, *acklevelstr, *ackbystr, *ackmsgstr;

			*endkey = '\0';
			cfg = get_nkconfig(bol, NKCONF_TIMEFILTER, NULL);
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

				if ( (newitem->config->priority > maxprio)  ||
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
					status = rbtInsert(rbstate, newitem->key, newitem);
				}
			}
		}

		bol = (eol ? (eol+1) : NULL);
	}

	return 0;
}


RbtHandle columnlist(RbtHandle statetree)
{
	RbtHandle rbcolumns;
	RbtIterator hhandle;

	rbcolumns = rbtNew(name_compare);
	for (hhandle = rbtBegin(statetree); (hhandle != rbtEnd(statetree)); hhandle = rbtNext(statetree, hhandle)) {
		void *k1, *k2;
		hstatus_t *itm;
		RbtStatus status;

	        rbtKeyValue(statetree, hhandle, &k1, &k2);
		itm = (hstatus_t *)k2;

		status = rbtInsert(rbcolumns, itm->testname, NULL);
	}

	return rbcolumns;
}

void print_colheaders(FILE *output, RbtHandle rbcolumns)
{
	int colcount;
	RbtIterator colhandle;

	colcount = 1;	/* Remember the hostname column */

	/* Group column headings */
	fprintf(output, "<TR>");
	fprintf(output, "<TD ROWSPAN=2>&nbsp;</TD>\n");	/* For the prio column - in both row headers+dash rows */
	fprintf(output, "<TD ROWSPAN=2>&nbsp;</TD>\n");	/* For the host column - in both row headers+dash rows */
	for (colhandle = rbtBegin(rbcolumns); (colhandle != rbtEnd(rbcolumns)); colhandle = rbtNext(rbcolumns, colhandle)) {
		void *k1, *k2;
		char *colname;

	        rbtKeyValue(rbcolumns, colhandle, &k1, &k2);
		colname = (char *)k1;
		colcount++;

		fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
		fprintf(output, " <A HREF=\"%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n",
			columnlink(colname), xgetenv("MKBBCOLFONT"), colname);
	}
	fprintf(output, "</TR>\n");
	fprintf(output, "<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n\n", colcount);
}

void print_hoststatus(FILE *output, hstatus_t *itm, RbtHandle columns, int prio, int firsthost)
{
	void *hinfo;
	char *dispname, *ip, *key;
	time_t now;
	RbtIterator colhandle;

	now = getcurrenttime(NULL);
	hinfo = hostinfo(itm->hostname);
	dispname = bbh_item(hinfo, BBH_DISPLAYNAME);
	ip = bbh_item(hinfo, BBH_IP);

	fprintf(output, "<TR>\n");

	/* Print the priority */
	fprintf(output, "<TD ALIGN=LEFT VALIGN=TOP WIDTH=25%% NOWRAP>");
	if (firsthost) 
		fprintf(output, "<FONT %s>PRIO %d</FONT>", xgetenv("MKBBROWFONT"), prio);
	else 
		fprintf(output, "&nbsp;");
	fprintf(output, "</TD>\n");

	/* Print the hostname with a link to the NK info page */
	fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", hostnamehtml(itm->hostname, NULL, 0));

	key = (char *)malloc(strlen(itm->hostname) + 1024);
	for (colhandle = rbtBegin(columns); (colhandle != rbtEnd(columns)); colhandle = rbtNext(columns, colhandle)) {
		void *k1, *k2;
		char *colname;
		RbtIterator sthandle;

		fprintf(output, "<TD ALIGN=CENTER>");

		rbtKeyValue(columns, colhandle, &k1, &k2);
		colname = (char *)k1;
		sprintf(key, "%s|%s", itm->hostname, colname);
		sthandle = rbtFind(rbstate, key);
		if (sthandle == rbtEnd(rbstate)) {
			fprintf(output, "-");
		}
		else {
			hstatus_t *column;
			char *htmlalttag;
			char *htmlackstr;

			rbtKeyValue(rbstate, sthandle, &k1, &k2);
			column = (hstatus_t *)k2;
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
					xgetenv("BBSKIN"), 
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


void print_oneprio(FILE *output, RbtHandle rbstate, RbtHandle rbcolumns, int prio)
{
	RbtIterator hhandle;
	int firsthost = 1;
	char *curhost = "";

	/* Then output each host and their column status */
	for (hhandle = rbtBegin(rbstate); (hhandle != rbtEnd(rbstate)); hhandle = rbtNext(rbstate, hhandle)) {
		void *k1, *k2;
		hstatus_t *itm;

	        rbtKeyValue(rbstate, hhandle, &k1, &k2);
		itm = (hstatus_t *)k2;

		if (itm->config->priority != prio) continue;
		if (strcmp(curhost, itm->hostname) == 0) continue;

		/* New host */
		curhost = itm->hostname;
		print_hoststatus(output, itm, rbcolumns, prio, firsthost);
		firsthost = 0;
	}

	/* If we did output any hosts, make some room for the next priority */
	if (!firsthost) fprintf(output, "<TR><TD>&nbsp;</TD></TR>\n");
}



void generate_nkpage(FILE *output, char *hfprefix)
{
	RbtIterator hhandle;
	int color = COL_GREEN;
	int maxprio = 0;

	/* Determine background color and max. priority */
	for (hhandle = rbtBegin(rbstate); (hhandle != rbtEnd(rbstate)); hhandle = rbtNext(rbstate, hhandle)) {
		void *k1, *k2;
		hstatus_t *itm;

	        rbtKeyValue(rbstate, hhandle, &k1, &k2);
		itm = (hstatus_t *)k2;

		if (itm->color > color) color = itm->color;
		if (itm->config->priority > maxprio) maxprio = itm->config->priority;
	}

        headfoot(output, hfprefix, "", "header", color);
        fprintf(output, "<center>\n");

        if (color != COL_GREEN) {
		RbtHandle rbcolumns;
		int prio;

		rbcolumns = columnlist(rbstate);

		fprintf(output, "<TABLE BORDER=0 CELLPADDING=4 SUMMARY=\"Critical status display\">\n");
		print_colheaders(output, rbcolumns);

		for (prio = 1; (prio <= maxprio); prio++) {
			print_oneprio(output, rbstate, rbcolumns, prio);
		}

		fprintf(output, "</TABLE>\n");
		rbtDelete(rbcolumns);
        }
        else {
                /* "All Monitored Systems OK */
		fprintf(output, "<FONT SIZE=+2 FACE=\"Arial, Helvetica\"><BR><BR><I>All Monitored Systems OK</I></FONT><BR><BR>");
        }

        fprintf(output, "</center>\n");
        headfoot(output, hfprefix, "", "footer", color);
}


static int maxprio = 3;
static time_t maxage = INT_MAX;
static int mincolor = COL_YELLOW;
static int wantacked = 0;

static void selectenv(char *name, char *val)
{
	char *env;
	char *p;

	env = (char *)malloc(strlen(name) + strlen(val) + 20);
	sprintf(env, "SELECT_%s_%s=SELECTED", name, val);
	for (p=env; (*p); p++) *p = toupper((int)*p);
	putenv(env);
}

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;
	int havemaxprio=0, havemaxage=0, havemincolor=0, havewantacked=0;

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

		cwalk = cwalk->next;
	}

	if (!havemaxprio)   selectenv("MAXPRIO", "3");
	if (!havemaxage)    selectenv("MAXAGE", "525600");
	if (!havemincolor)  selectenv("MINCOLOR", "yellow");
	if (!havewantacked) selectenv("WANTACKED", "no");
}


int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--nkacklevel=")) {
			char *p = strchr(argv[argi], '=');
			nkacklevel = atoi(p+1);
		}
	}

	redirect_cgilog("hobbit-nkview");

	setdocurl(hostsvcurl("%s", xgetenv("INFOCOLUMN"), 1));

	parse_query();
	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
	load_nkconfig(NULL);
	load_all_links();
	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	if (loadstatus(maxprio, maxage, mincolor, wantacked) == 0) {
		use_recentgifs = 1;
		generate_nkpage(stdout, "hobbitnk");
	}
	else {
		fprintf(stdout, "Cannot load Hobbit status\n");
	}

	return 0;
}

