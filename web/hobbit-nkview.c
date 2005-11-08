/*----------------------------------------------------------------------------*/
/* Hobbit CGI for generating the Hobbit NK page                               */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-nkview.c,v 1.1 2005-11-08 13:42:27 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>

#include "libbbgen.h"

typedef struct nkconf_t {
	char *key;
	int priority;
	char *resolvergroup;
	char *ttgroup;
	char *ttextra;
} nkconf_t;
static RbtHandle rbconf;

typedef struct hstatus_t {
	char *hostname;
	char *testname;
	char *key;
	int color;
	time_t lastchange, logtime, validtime, acktime;
	nkconf_t *config;
} hstatus_t;

static RbtHandle rbstate;


void errormsg(char *s)
{
	fprintf(stderr, "%s\n", s);
}


static int key_compare(void *a, void *b)
{
	return strcasecmp((char *)a, (char *)b);
}


void loadconfig(char *fn, char *wantclass)
{
	FILE *fd;
	char *inbuf = NULL;
	int inbufsz = 0;

	rbconf = rbtNew(key_compare);

	fd = stackfopen(fn, "r");
	if (fd == NULL) {
		errormsg("Cannot open configuration file\n");
		return;
	}

	while (stackfgets(&inbuf, &inbufsz, "include", NULL)) {
		/* Class  Host  service  TIME  Resolvergroup TTPrio TTGroup TTExtra */
		char *eclass, *ehost, *eservice, *etime, *rgroup, *ttgroup, *ttextra;
		int ttprio = 0;
		nkconf_t *newitem;
		RbtStatus status;

		eclass = gettok(inbuf, "|\n"); if (!eclass) continue;
		if (wantclass && eclass && (strcmp(eclass, wantclass) != 0)) continue;
		ehost = gettok(NULL, "|\n"); if (!ehost) continue;
		eservice = gettok(NULL, "|\n"); if (!eservice) continue;
		etime = gettok(NULL, "|\n"); if (!etime) continue;
		rgroup = gettok(NULL, "|\n");
		ttprio = atoi(gettok(NULL, "|\n"));
		ttgroup = gettok(NULL, "|\n");
		ttextra = gettok(NULL, "|\n");

		if ((ehost == NULL) || (eservice == NULL) || (ttprio == 0)) continue;
		if (etime && *etime && !within_sla(etime, 0)) continue;

		newitem = (nkconf_t *)malloc(sizeof(nkconf_t));
		newitem->key = (char *)malloc(strlen(ehost) + strlen(eservice) + 2);
		sprintf(newitem->key, "%s|%s", ehost, eservice);
		newitem->priority = ttprio;
		newitem->resolvergroup = strdup(urlencode(rgroup));
		newitem->ttgroup = strdup(urlencode(ttgroup));
		newitem->ttextra = strdup(urlencode(ttextra));

		status = rbtInsert(rbconf, newitem->key, newitem);
	}

	stackfclose(fd);
}

void loadstatus(void)
{
	int hobbitdresult;
	char *board = NULL;
	char *bol, *eol;

	hobbitdresult = sendmessage("hobbitdboard color=red,yellow fields=hostname,testname,color,lastchange,logtime,validtime,acktime", NULL, NULL, &board, 1, BBTALK_TIMEOUT);
	if (hobbitdresult != BB_OK) {
		errormsg("Unable to fetch current status\n");
		return;
	}

	rbstate = rbtNew(key_compare);

	bol = board;
	while (bol && (*bol)) {
		char *endkey;
		RbtStatus status;
		RbtIterator handle;

		eol = strchr(bol, '\n'); if (eol) *eol = '\0';

		/* Find the config entry */
		endkey = strchr(bol, '|'); if (endkey) endkey = strchr(endkey+1, '|'); 
		if (endkey) {
			*endkey = '\0';
			handle = rbtFind(rbconf, bol);
			*endkey = '|';

			if (handle != rbtEnd(rbconf)) {
				hstatus_t *newitem = (hstatus_t *)malloc(sizeof(hstatus_t));
				void *k1, *k2;

				rbtKeyValue(rbconf, handle, &k1, &k2);

				newitem->hostname   = gettok(bol, "|");
				newitem->testname   = gettok(NULL, "|");
				newitem->key        = (char *)malloc(strlen(newitem->hostname) + strlen(newitem->testname) + 2);
				sprintf(newitem->key, "%s|%s", newitem->hostname, newitem->testname);
				newitem->config     = (nkconf_t *)k2;
				newitem->color      = parse_color(gettok(NULL, "|"));
				newitem->lastchange = atoi(gettok(NULL, "|"));
				newitem->logtime    = atoi(gettok(NULL, "|"));
				newitem->validtime  = atoi(gettok(NULL, "|"));
				newitem->acktime    = atoi(gettok(NULL, "|"));
				status = rbtInsert(rbstate, newitem->key, newitem);
			}
		}

		bol = (eol ? (eol+1) : NULL);
	}
}


RbtHandle columnlist(RbtHandle statetree, int prio)
{
	RbtHandle rbcolumns;
	RbtIterator hhandle;

	rbcolumns = rbtNew(key_compare);
	for (hhandle = rbtBegin(statetree); (hhandle != rbtEnd(statetree)); hhandle = rbtNext(statetree, hhandle)) {
		void *k1, *k2;
		hstatus_t *itm;
		RbtStatus status;

	        rbtKeyValue(statetree, hhandle, &k1, &k2);
		itm = (hstatus_t *)k2;

		if ((prio != -1) && (itm->config->priority != prio)) continue;

		status = rbtInsert(rbcolumns, itm->testname, NULL);
	}

	return rbcolumns;
}

static char *nameandcomment(namelist_t *host)
{
	static char *result = NULL;
	char *cmt, *disp, *hname;

	if (result) xfree(result);

	hname = bbh_item(host, BBH_HOSTNAME);
	disp = bbh_item(host, BBH_DISPLAYNAME);
	cmt = bbh_item(host, BBH_COMMENT);
	if (disp == NULL) disp = hname;

	if (cmt) {
		result = (char *)malloc(strlen(disp) + strlen(cmt) + 4);
		sprintf(result, "%s (%s)", disp, cmt);
		return result;
	}
	else 
		return disp;
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
	namelist_t *hinfo;
	char *dispname, *ip, *key;
	time_t now;
	RbtIterator colhandle;

	now = time(NULL);
	hinfo = hostinfo(itm->hostname);
	dispname = bbh_item(hinfo, BBH_DISPLAYNAME);
	ip = bbh_item(hinfo, BBH_IP);

	fprintf(output, "<TR>\n");

	/* Print the priority */
	fprintf(output, "<TD ALIGN=LEFT VALIGN=TOP WIDTH=25%%>");
	if (firsthost) 
		fprintf(output, "<FONT %s>PRIO %d</FONT>", xgetenv("MKBBROWFONT"), prio);
	else 
		fprintf(output, "&nbsp;");
	fprintf(output, "</TD>\n");

	/* Print the hostname with a link to the NK info page */
	fprintf(output, "<TD ALIGN=LEFT>");
	fprintf(output, "<A HREF=\"%s/bb-hostsvc.sh?HOSTSVC=%s.%s&amp;IP=%s&amp;DISPLAYNAME=%s\">",
		xgetenv("CGIBINURL"), commafy(itm->hostname), xgetenv("INFOCOLUMN"),
		ip, (dispname ? dispname : itm->hostname));
	fprintf(output, "<FONT %s>%s</FONT>", xgetenv("MKBBROWFONT"), nameandcomment(hinfo));
	fprintf(output, "</A>");
	fprintf(output, "</TD>\n");

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

			rbtKeyValue(rbstate, sthandle, &k1, &k2);
			column = (hstatus_t *)k2;
			if (column->config->priority != prio) 
				fprintf(output, "-");
			else {
				time_t age = now - column->lastchange;
				htmlalttag = alttag(colname, column->color, 0, 1, agestring(age));
				fprintf(output, "<A HREF=\"%s/bb-hostsvc.sh?HOSTSVC=%s.%s&amp;IP=%s&amp;DISPLAYNAME=%s&amp;NKPRIO=%d&amp;NKRESOLVER=%s&amp;NKTTGROUP=%s&amp;NKTTEXTRA=%s\">",
					xgetenv("CGIBINURL"), commafy(itm->hostname), colname,
					ip, (dispname ? dispname : itm->hostname),
					prio, 
					column->config->resolvergroup, 
					column->config->ttgroup,
					column->config->ttextra);
				fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
					xgetenv("BBSKIN"), dotgiffilename(column->color, 0, (age > 3600)),
					htmlalttag, htmlalttag, 
					xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
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

	fprintf(output, "<TR><TD>&nbsp;</TD></TR>\n");
}



void generate_nkpage(FILE *output, char *hfprefix, int priolimit)
{
	RbtIterator hhandle;
	int color = COL_GREEN;

	/* Determine background color and max. priority */
	for (hhandle = rbtBegin(rbstate); (hhandle != rbtEnd(rbstate)); hhandle = rbtNext(rbstate, hhandle)) {
		void *k1, *k2;
		hstatus_t *itm;
		RbtStatus status;

	        rbtKeyValue(rbstate, hhandle, &k1, &k2);
		itm = (hstatus_t *)k2;

		if (itm->color > color) color = itm->color;
	}

        headfoot(output, hfprefix, "", "header", color);
        fprintf(output, "<center>\n");

        if (color != COL_GREEN) {
		RbtHandle rbcolumns;
		int prio;

		rbcolumns = columnlist(rbstate, -1);

		fprintf(output, "<TABLE BORDER=0 CELLPADDING=4>\n");
		print_colheaders(output, rbcolumns);

		for (prio = 1; (prio <= priolimit); prio++) {
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

int main(int argc, char *argv[])
{
	char configfn[PATH_MAX];

	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
	sprintf(configfn, "%s/etc/hobbitnk.cfg", xgetenv("BBHOME"));
	loadconfig(configfn, NULL);
	load_all_links();
	loadstatus();
	use_recentgifs = 1;

	fprintf(stdout, "Content-type: text/html\n\n");
	generate_nkpage(stdout, "bbnk", 3);

	return 0;
}

