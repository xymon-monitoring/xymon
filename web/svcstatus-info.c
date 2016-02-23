/*----------------------------------------------------------------------------*/
/* Xymon "info" column generator.                                             */
/*                                                                            */
/* This is a standalone tool for generating data for the "info" column data.  */
/* This extracts all of the static configuration info about a host contained  */
/* in Xymon, and displays it on one webpage.                                  */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

/* The following is for the DNS lookup we perform on DHCP addresses */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "libxymon.h"

int showenadis = 1;
int usejsvalidation = 1;
int newcritconfig = 1;

typedef struct hinf_t {
	char *name;
	int color;
	char *dismsg;
	time_t distime, lastchange;
	struct hinf_t *next;
} hinf_t;
hinf_t *tnames = NULL;
int testcount = 0;
char *unametxt = NULL;
char *clientvertxt = NULL;

typedef struct sched_t {
	int id;
	time_t when;
	char *srcip, *cmd;
	struct sched_t *next;
} sched_t;
sched_t *schedtasks = NULL;

static int test_name_compare(const void *v1, const void *v2)
{
	hinf_t *r1 = (hinf_t *)v1;
	hinf_t *r2 = (hinf_t *)v2;

	return strcmp(r1->name, r2->name);
}

static int fetch_status(char *hostname)
{
	char *commaname;
	char *statuslist = NULL;
	char *xymoncmd = (char *)malloc(1024 + strlen(hostname));
	char *walk;
	int testsz;
	int haveuname = 0;
	sendreturn_t *sres;

	sres = newsendreturnbuf(1, NULL);
	sprintf(xymoncmd, "xymondboard fields=testname,color,disabletime,dismsg,client,lastchange host=^%s$", hostname);
	if (sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		return 1;
	}
	else {
		statuslist = getsendreturnstr(sres, 1);
	}

	testsz = 10;
	tnames = (hinf_t *)malloc(testsz * sizeof(hinf_t));

	walk = statuslist;
	testcount = 0;
	while (walk) {
		char *eol, *tok;

		eol = strchr(walk, '\n'); if (eol) *eol = '\0';

		tok = gettok(walk, "|");
		if ( tok && (strcmp(tok, xgetenv("INFOCOLUMN")) != 0) && (strcmp(tok, xgetenv("TRENDSCOLUMN")) != 0) && (strcmp(tok, xgetenv("CLIENTCOLUMN")) != 0) ) {
			tnames[testcount].name = strdup(htmlquoted(tok)); tok = gettok(NULL, "|"); 
			if (tok) { tnames[testcount].color = parse_color(tok); tok = gettok(NULL, "|"); }
			if (tok) { tnames[testcount].distime = atol(tok); tok = gettok(NULL, "|"); }
			if (tok) { tnames[testcount].dismsg = strdup(tok); tok = gettok(NULL, "|"); }	/* nlencoded, so htmlrequote later */
			if (tok) { haveuname |= (*tok == 'Y'); tok = gettok(NULL, "|"); }
			if (tok) { tnames[testcount].lastchange = atol(tok); }
			tnames[testcount].next = NULL;
			testcount++;
			if (testcount == testsz) {
				testsz += 10;
				tnames = (hinf_t *)realloc(tnames, (testsz * sizeof(hinf_t)));
			}
		}

		if (eol) {
			walk = eol + 1;
		}
		else {
			walk = NULL;
		}
	}

	/* Sort them so the display looks prettier */
	qsort(&tnames[0], testcount, sizeof(hinf_t), test_name_compare);
	if (statuslist) xfree(statuslist); statuslist = NULL;


	sprintf(xymoncmd, "schedule");
	if (sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		return 1;
	}
	else {
		statuslist = getsendreturnstr(sres, 1);
	}

	commaname = strdup(commafy(hostname));
	walk = statuslist;
	while (walk) {
		char *eol, *tok = NULL;

		eol = strchr(walk, '\n'); if (eol) *eol = '\0';

		/* Not quite fool-proof, but filters out most of the stuff that does not belong to this host. */
		if (strstr(walk, hostname) || strstr(walk, commaname)) tok = gettok(walk, "|");

		if (tok && strlen(tok)) {
			sched_t *newitem = (sched_t *)calloc(1, sizeof(sched_t));

			newitem->id = atoi(tok); tok = gettok(NULL, "|");
			if (tok) { newitem->when  = (int)atoi(tok); tok = gettok(NULL, "|"); }
			if (tok) { newitem->srcip = strdup(tok);    tok = gettok(NULL, "\n"); }
			if (tok) newitem->cmd = strdup(tok);

			if (newitem->id && newitem->when && newitem->srcip && newitem->cmd) {
				newitem->next = schedtasks;
				schedtasks = newitem;
			}
			else {
				if (newitem->cmd) xfree(newitem->cmd);
				if (newitem->srcip) xfree(newitem->srcip);
				xfree(newitem);
			}
		}

		if (eol) {
			walk = eol + 1;
		}
		else {
			walk = NULL;
		}
	}

	if (haveuname) {
		char *clidata = NULL;
		char *boln, *eoln, *htmlq;

		sprintf(xymoncmd, "clientlog %s section=uname,osversion,clientversion", hostname);
		if (sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
			return 1;
		}
		else {
			clidata = getsendreturnstr(sres, 1);
		}

		boln = strstr(clidata, "[osversion]\n");
		if (boln) {
			boln = strchr(boln, '\n') + 1;
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			unametxt = strdup(htmlquoted(boln));
			if (eoln) *eoln = '\n';
		}

		boln = strstr(clidata, "[uname]\n");
		if (boln) {
			boln = strchr(boln, '\n') + 1;
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			htmlq = htmlquoted(boln);
			if (unametxt) {
				unametxt = (char *)realloc(unametxt, strlen(unametxt) + strlen(htmlq) + 6);
				strcat(unametxt, "<br>\n");
				strcat(unametxt, htmlq);
			}
			else {
				unametxt = strdup(htmlq);
			}
			if (eoln) *eoln = '\n';
		}

		boln = strstr(clidata, "[clientversion]\n");
		if (boln) {
			boln = strchr(boln, '\n') + 1;
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			htmlq = htmlquoted(boln);
			clientvertxt = strdup(htmlq);
			if (eoln) *eoln = '\n';
		}

		xfree(clidata);
	}

	freesendreturnbuf(sres);

	return 0;
}

static void generate_xymon_alertinfo(char *hostname, strbuffer_t *buf)
{
	void *hi = hostinfo(hostname);
	activealerts_t *alert;
	int i, rcount;

	addtobuffer(buf, "<table summary=\"");
	addtobuffer(buf, htmlquoted(hostname));
	addtobuffer(buf, " Alerts\" border=1>\n");
	addtobuffer(buf, "<tr><th>Service</th><th>Recipient</th><th>1st Delay</th><th>Stop after</th><th>Repeat</th><th>Time of Day</th><th>Colors</th></tr>\n");

	alert = calloc(1, sizeof(activealerts_t));
	alert->hostname = hostname;
	alert->pagepath = (hi ? xmh_item(hi, XMH_ALLPAGEPATHS) : "");
	alert->ip = strdup("127.0.0.1");
	alert->color = COL_RED;
	alert->pagemessage = "";
	alert->state = A_PAGING;
	strcpy(alert->cookie, "12345");
	rcount = 0;

	alert_printmode(1);
	for (i = 0; (i < testcount); i++) {
		alert->testname = tnames[i].name;
		if (have_recipient(alert, NULL)) { rcount++; print_alert_recipients(alert, buf); }
	}

	if (rcount == 0) {
		/* No alerts defined. */
		addtobuffer(buf, "<tr><td colspan=9 align=center><b><i>No alerts defined</i></b></td></tr>\n");
	}

	addtobuffer(buf, "</table>\n");

	xfree(alert);

}

static void generate_xymon_holidayinfo(char *hostname, strbuffer_t *buf)
{
	void *hi = hostinfo(hostname);
	time_t now = getcurrenttime(NULL);
	struct tm *tm;
	int month, year;
	int needreload = 0;
	char *holidayset;
	char yeartxt[10];

	tm = localtime(&now);
	year = tm->tm_year + 1900;
	month = tm->tm_mon;

	holidayset = xmh_item(hi, XMH_HOLIDAYS);

	addtobuffer(buf, "<table summary=\"");
	addtobuffer(buf, htmlquoted(hostname));
	addtobuffer(buf, " Holidays\" border=1>\n");

	addtobuffer(buf, "<tr>");
	addtobuffer(buf, "<td><table>\n");

	switch (month) {
	  case 0: case 1: case 2:
		sprintf(yeartxt, "%d/%d", year-1, year);
		break;

	  case 9: case 10: case 11:
		sprintf(yeartxt, "%d/%d", year, year+1);
		break;

	  default:
		sprintf(yeartxt, "%d", year);
		break;
	}

	if (holidayset) {
		addtobuffer(buf, "<tr><th colspan=2>Holidays ");
		addtobuffer(buf, yeartxt);
		addtobuffer(buf, " (");
		addtobuffer(buf, holidayset);
		addtobuffer(buf, ")</th></tr>\n");
	}
	else {
		addtobuffer(buf, "<tr><th colspan=2>Holidays ");
		addtobuffer(buf, yeartxt);
		addtobuffer(buf, "</th></tr>\n");
	}


	/* In January+February+March, show last year's holidays in October+November+December */
	if (month <= 2) {
		load_holidays(year-1);
		printholidays(holidayset, buf, 9, 11);
		load_holidays(year);
	}

	if (month >= 9) {
		/* 
		 * In October thru December, skip the first half 
		 * of this year from the display. Instead, show
		 * holidays in first half of next year.
		 */
		printholidays(holidayset, buf, 6, 11);

		load_holidays(year+1);
		printholidays(holidayset, buf, 0, 5);
		load_holidays(year);
	}
	else {
		printholidays(holidayset, buf, 0, 11);
	}

	addtobuffer(buf, "</table></td>\n");

	addtobuffer(buf, "</tr>\n");

	addtobuffer(buf, "</table>\n");

	if (needreload) load_holidays(0);
}



static void generate_xymon_statuslist(char *hostname, strbuffer_t *buf)
{
	int i, btncount;
	char *xymondatefmt = xgetenv("XYMONDATEFORMAT");
	strbuffer_t *servRed, *servYellow, *servPurple, *servBlue;

	servRed = newstrbuffer(0);
	servYellow = newstrbuffer(0);
	servPurple = newstrbuffer(0);
	servBlue = newstrbuffer(0);

	addtobuffer(buf, "<tr><th align=left valign=top>Status summary</th><td align=left>\n");
	addtobuffer(buf, "<form name=\"colorsel\" action=\"nosubmit\" method=\"GET\">\n");
	addtobuffer(buf, "<table summary=\"Status summary\" border=1>\n");
	addtobuffer(buf, "<tr><th>Service</th><th>Since</th><th>Duration</th></tr>\n");

	for (i = 0; i < testcount; i++) {
		char tstr[1024];
		time_t logage = getcurrenttime(NULL) - tnames[i].lastchange;

		addtobuffer(buf, "<tr>");

		addtobuffer(buf, "<td><a href=\"");
		addtobuffer(buf, hostsvcurl(hostname, tnames[i].name, 1));
		addtobuffer(buf, "\"><img src=\"");
		addtobuffer(buf, xgetenv("XYMONSKIN"));
		addtobuffer(buf, "/");
		addtobuffer(buf, dotgiffilename(tnames[i].color, 0, 1));
		addtobuffer(buf, "\" height=\"");
		addtobuffer(buf, xgetenv("DOTHEIGHT"));
		addtobuffer(buf, "\" width=\"");
		addtobuffer(buf, xgetenv("DOTWIDTH"));
		addtobuffer(buf, "\" border=0 alt=\"");
		addtobuffer(buf, colorname(tnames[i].color));
		addtobuffer(buf, " status\"></a> ");
		addtobuffer(buf, tnames[i].name);
		addtobuffer(buf, "</td>");

		strftime(tstr, sizeof(tstr), xymondatefmt, localtime(&tnames[i].lastchange));
		addtobuffer(buf, "<td>");
		addtobuffer(buf, tstr);
		addtobuffer(buf, "</td>");

		snprintf(tstr, sizeof(tstr), "<td align=right>%d days, %02d hours, %02d minutes</td>",
			(int)(logage / 86400),(int) ((logage % 86400) / 3600),(int) ((logage % 3600) / 60));
		addtobuffer(buf, tstr);

		addtobuffer(buf, "</tr>\n");

		switch (tnames[i].color) {
		  case COL_BLUE   : addtobuffer(servBlue, ",");   addtobuffer(servBlue, tnames[i].name);   break;
		  case COL_RED    : addtobuffer(servRed, ",");    addtobuffer(servRed, tnames[i].name);    break;
		  case COL_YELLOW : addtobuffer(servYellow, ","); addtobuffer(servYellow, tnames[i].name); break;
		  case COL_PURPLE : addtobuffer(servPurple, ","); addtobuffer(servPurple, tnames[i].name); break;
		}
	}

	btncount = 0;
	if (STRBUFLEN(servRed) > 0)    btncount++;
	if (STRBUFLEN(servYellow) > 0) btncount++;
	if (STRBUFLEN(servPurple) > 0) btncount++;
	if (STRBUFLEN(servBlue) > 0)   btncount++;
	if (btncount > 0) {
		char numstr[10];

		snprintf(numstr, sizeof(numstr)-1, "%d", btncount);

		addtobuffer(buf, "<tr><td colspan=3>\n");

		addtobuffer(buf, "<table width=\"100%\">\n");
		addtobuffer(buf, "<tr><th colspan=");
		addtobuffer(buf, numstr);
		addtobuffer(buf, "><center><i>Toggle tests to disable</i></center></th></tr>\n");

		addtobuffer(buf, "<tr>\n");
		if (STRBUFLEN(servRed) > 0) {
			addtobuffer(buf, "<td align=center><input type=button value=\"Toggle red\" onClick=\"mark4Disable('");
			addtostrbuffer(buf, servRed);
			addtobuffer(buf, ",');\"></td>\n");
		} 
		if (STRBUFLEN(servYellow) > 0) {
			addtobuffer(buf, "<td align=center><input type=button value=\"Toggle yellow\" onClick=\"mark4Disable('");
			addtostrbuffer(buf, servYellow);
			addtobuffer(buf, ",');\"></td>\n");
		} 
		if (STRBUFLEN(servPurple) > 0) {
			addtobuffer(buf, "<td align=center><input type=button value=\"Toggle purple\" onClick=\"mark4Disable('");
			addtostrbuffer(buf, servPurple);
			addtobuffer(buf, ",');\"></td>\n");
		} 
		if (STRBUFLEN(servBlue) > 0) {
			addtobuffer(buf, "<td align=center><input type=button value=\"Toggle blue\" onClick=\"mark4Disable('");
			addtostrbuffer(buf, servBlue);
			addtobuffer(buf, ",');\"></td>\n");
		} 

		addtobuffer(buf, "</tr>\n");
		addtobuffer(buf, "</table>\n");

		addtobuffer(buf,"</td></tr>\n");

	}

	addtobuffer(buf,"</table></form>\n");
	addtobuffer(buf, "</td></tr>\n");
	addtobuffer(buf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	freestrbuffer(servRed);
	freestrbuffer(servYellow);
	freestrbuffer(servPurple);
	freestrbuffer(servBlue);
}

static void generate_xymon_disable(char *hostname, strbuffer_t *buf)
{
	int i;
	time_t now = getcurrenttime(NULL);
	int beginyear, endyear;
	struct tm monthtm;
	struct tm *nowtm;
	char mname[20];
	char *selstr;

	nowtm = localtime(&now);
	beginyear = nowtm->tm_year + 1900;
	endyear = nowtm->tm_year + 1900 + 5;

	addtobuffer(buf, "<form name=\"disableform\" method=\"post\" action=\"");
	addtobuffer(buf, xgetenv("SECURECGIBINURL"));
	addtobuffer(buf, "/enadis.sh\">\n");

	addtobuffer(buf, "<table summary=\"");
	addtobuffer(buf, htmlquoted(hostname));
	addtobuffer(buf, " disable\" border=1>\n");

	addtobuffer(buf, "<tr>\n");

	addtobuffer(buf, "<td rowspan=2><select multiple size=\"15\" name=\"disabletest\">\n");
	addtobuffer(buf, "<option value=\"*\">ALL</option>\n");
	for (i=0; (i < testcount); i++) {
		char *colstyle;
		switch (tnames[i].color) {
		  case COL_RED:	   colstyle = "color: red"; break;
		  case COL_YELLOW: colstyle = "color: #FFDE0F"; break;
		  case COL_GREEN:  colstyle = "color: green"; break;
		  case COL_BLUE:   colstyle = "color: blue;"; break;
		  case COL_PURPLE: colstyle = "color: fuchsia;"; break;
		  default:         colstyle = "color: black;"; break;
		}

		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, tnames[i].name);
		addtobuffer(buf, "\" style=\"");
		addtobuffer(buf, colstyle);
		addtobuffer(buf, "\">");
		addtobuffer(buf, tnames[i].name);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</select></td>\n");

	addtobuffer(buf, "<td>\n");
	addtobuffer(buf, "   <table summary=\"Disable parameters\" border=0>\n");
	addtobuffer(buf, "      <tr> <td>Cause: <input name=\"cause\" type=text size=50 maxlength=80></td> </tr>\n");
	addtobuffer(buf, "      <tr>\n");
	addtobuffer(buf, "         <td align=center width=\"90%\">\n");
	addtobuffer(buf, "            <table summary=\"Until when to disable\" border=1>\n");
	addtobuffer(buf, "              <tr><td align=left><input name=go2 type=radio value=\"Disable for\" checked> Disable for\n");
	addtobuffer(buf, "         <input name=\"duration\" type=text size=5 maxlength=5 value=\"4\"> &nbsp;\n");
	addtobuffer(buf, "            <select name=\"scale\">\n");
	addtobuffer(buf, "               <option value=1>minutes</option>\n");
	addtobuffer(buf, "               <option value=60 selected>hours</option>\n");
	addtobuffer(buf, "               <option value=1440>days</option>\n");
	addtobuffer(buf, "               <option value=10080>weeks</option>\n");
	addtobuffer(buf, "            </select>\n");
	addtobuffer(buf, "        </td>\n</tr>\n");
	addtobuffer(buf, "<br>\n");
	
/* Until start */
	/* Months */

	addtobuffer(buf, "              <tr><td align=left><input name=go2 type=radio value=\"Disable until\"> Disable until\n");
	addtobuffer(buf, "                    <br>\n");
	addtobuffer(buf, "<SELECT NAME=\"endmonth\" onClick=\"setcheck(this.form.go2,true)\">\n");
	for (i=1; (i <= 12); i++) {
		char istr[3];

		sprintf(istr, "%d", i);

		if (i == (nowtm->tm_mon + 1)) selstr = " selected"; else selstr = "";
		monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
		monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
		strftime(mname, sizeof(mname)-1, "%B", &monthtm);

		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, mname);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Days */
	addtobuffer(buf, "<SELECT NAME=\"endday\" onClick=\"setcheck(this.form.go2,true)\">\n");
	for (i=1; (i <= 31); i++) {
		char istr[3];

		sprintf(istr, "%d", i);

		if (i == nowtm->tm_mday) selstr = " selected"; else selstr = "";

		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Years */
	addtobuffer(buf, "<SELECT NAME=\"endyear\" onClick=\"setcheck(this.form.go2,true)\">\n");
	for (i=beginyear; (i <= endyear); i++) {
		char istr[5];

		sprintf(istr, "%d", i);

		if (i == (nowtm->tm_year + 1900)) selstr = " selected"; else selstr = "";

		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Hours */
	addtobuffer(buf, "<SELECT NAME=\"endhour\" onClick=\"setcheck(this.form.go2,true)\">\n");
	for (i=0; (i <= 24); i++) {
		char istr[3];

		sprintf(istr, "%d", i);

		if (i == nowtm->tm_hour) selstr = " selected"; else selstr = "";
		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Minutes */
	addtobuffer(buf, "<SELECT NAME=\"endminute\" onClick=\"setcheck(this.form.go2,true)\">\n");
	for (i=0; (i <= 59); i++) {
		char istr[3];

		sprintf(istr, "%02d", i);

		if (i == nowtm->tm_min) selstr = " selected"; else selstr = "";
		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	addtobuffer(buf, "            &nbsp;&nbsp;-&nbsp;OR&nbsp;-&nbsp;until&nbsp;OK:<input name=\"untilok\" type=checkbox onClick=\"setcheck(this.form.go2,true)\">");
	addtobuffer(buf, "              </td></tr>\n");
	addtobuffer(buf, "            </table> \n");

/* Until end */

	addtobuffer(buf, "              </td>\n</tr>\n");

	addtobuffer(buf, "      <tr> <td>&nbsp;</td> </tr>\n");
 
	addtobuffer(buf, "      <tr>\n");
	addtobuffer(buf, "         <td align=center width=\"90%\">\n");
	addtobuffer(buf, "            <table summary=\"When to disable\" border=1>\n");
	addtobuffer(buf, "              <tr><td align=left><input name=go type=radio value=\"Disable now\" checked> Disable now</td></tr>\n");
	addtobuffer(buf, "              <tr><td align=left><input name=go type=radio value=\"Schedule disable\"> Schedule disable at\n");
	addtobuffer(buf, "                    <br>\n");

	/* Months */
	addtobuffer(buf, "<SELECT NAME=\"month\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=1; (i <= 12); i++) {
		char istr[3];

		sprintf(istr, "%d", i);

		if (i == (nowtm->tm_mon + 1)) selstr = " selected"; else selstr = "";
		monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
		monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
		strftime(mname, sizeof(mname)-1, "%B", &monthtm);

		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, mname);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Days */
	addtobuffer(buf, "<SELECT NAME=\"day\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=1; (i <= 31); i++) {
		char istr[3];

		sprintf(istr, "%d", i);

		if (i == nowtm->tm_mday) selstr = " selected"; else selstr = "";

		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Years */
	addtobuffer(buf, "<SELECT NAME=\"year\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=beginyear; (i <= endyear); i++) {
		char istr[5];

		sprintf(istr, "%d", i);

		if (i == (nowtm->tm_year + 1900)) selstr = " selected"; else selstr = "";

		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Hours */
	addtobuffer(buf, "<SELECT NAME=\"hour\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=0; (i <= 24); i++) {
		char istr[3];

		sprintf(istr, "%d", i);

		if (i == nowtm->tm_hour) selstr = " selected"; else selstr = "";
		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Minutes */
	addtobuffer(buf, "<SELECT NAME=\"minute\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=0; (i <= 59); i++) {
		char istr[3];

		sprintf(istr, "%02d", i);

		if (i == nowtm->tm_min) selstr = " selected"; else selstr = "";
		addtobuffer(buf, "<option value=\"");
		addtobuffer(buf, istr);
		addtobuffer(buf, "\"");
		addtobuffer(buf, selstr);
		addtobuffer(buf, ">");
		addtobuffer(buf, istr);
		addtobuffer(buf, "</option>\n");
	}
	addtobuffer(buf, "</SELECT>\n");
	addtobuffer(buf, "              </td></tr>\n");
	addtobuffer(buf, "            </table> \n");
	addtobuffer(buf, "         </td>\n");
	addtobuffer(buf, "      </tr>\n");
	if (usejsvalidation) {
		addtobuffer(buf, "      <tr> <td align=center> <input name=apply type=\"button\" onClick=\"validateDisable(this.form)\" value=\"Apply\"></td> </tr>\n");
	}
	else {
		addtobuffer(buf, "      <tr> <td align=center> <input name=apply type=\"submit\" value=\"Apply\"></td> </tr>\n");
	}
	addtobuffer(buf, "   </table>\n");
	addtobuffer(buf, "</td>\n");


	addtobuffer(buf, "</table>\n");

	addtobuffer(buf, "<input name=\"hostname\" type=hidden value=\"");
	addtobuffer(buf, htmlquoted(hostname));
	addtobuffer(buf, "\">\n");

	addtobuffer(buf, "</form>\n");
}

static void generate_xymon_enable(char *hostname, strbuffer_t *buf)
{
	int i;
	char *msg, *eoln;

	addtobuffer(buf, "<table summary=\"");
	addtobuffer(buf, htmlquoted(hostname));
	addtobuffer(buf, " disabled tests\" border=1>\n");

	addtobuffer(buf, "<tr><th>Test</th><th>Disabled until</th><th>Cause</th><th>&nbsp;</th></tr>\n");

	for (i=0; (i < testcount); i++) {
		char istr[10];

		if (tnames[i].distime == 0) continue;

		snprintf(istr, sizeof(istr), "%d", i);

		addtobuffer(buf, "<tr>\n");

		addtobuffer(buf, "<td>");
		addtobuffer(buf, tnames[i].name);
		addtobuffer(buf, "</td>\n");
		addtobuffer(buf, "<td>");
		addtobuffer(buf, (tnames[i].distime == -1) ? "OK" : ctime(&tnames[i].distime));
		addtobuffer(buf, "</td>\n");

		/* Add an HTML'ized form of the disable-message */
		msg = tnames[i].dismsg; nldecode(msg); msg += strspn(msg, "0123456789 \t\n");
		addtobuffer(buf, "<td>");
		while ((eoln = strchr(msg, '\n')) != NULL) {
			*eoln = '\0';
			addtobuffer(buf, htmlquoted(msg));
			addtobuffer(buf, "<br>");
			msg = (eoln + 1);
		}
		addtobuffer(buf, htmlquoted(msg));
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "<td>");
		addtobuffer(buf, "<form method=\"post\" action=\"");
		addtobuffer(buf, xgetenv("SECURECGIBINURL"));
		addtobuffer(buf, "/enadis.sh\">\n");

		addtobuffer(buf, "<input name=\"hostname\" type=hidden value=\"");
		addtobuffer(buf, htmlquoted(hostname));
		addtobuffer(buf, "\">\n");

		addtobuffer(buf, "<input name=\"enabletest\" type=hidden value=\"");
		addtobuffer(buf, tnames[i].name);
		addtobuffer(buf, "\">\n");

		addtobuffer(buf, "<input name=\"go\" type=submit value=\"Enable\">\n");
		addtobuffer(buf, "</form>\n");
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "</tr>\n");
	}

	addtobuffer(buf, "<tr><td>ALL</td><td>&nbsp;</td><td>&nbsp;</td>\n");

	addtobuffer(buf, "<td>");
	addtobuffer(buf, "<form method=\"post\" action=\"");
	addtobuffer(buf, xgetenv("SECURECGIBINURL"));
	addtobuffer(buf, "/enadis.sh\">\n");

	addtobuffer(buf, "<input name=\"hostname\" type=hidden value=\"");
	addtobuffer(buf, htmlquoted(hostname));
	addtobuffer(buf, "\">\n");

	addtobuffer(buf, "<input name=\"enabletest\" type=hidden value=\"*\">\n");
	addtobuffer(buf, "<input name=\"go\" type=submit value=\"Enable\">\n");
	addtobuffer(buf, "</form>\n");
	addtobuffer(buf, "</td>\n");

	addtobuffer(buf, "</tr>\n");

	addtobuffer(buf, "</table>\n");
}


static void generate_xymon_scheduled(char *hostname, strbuffer_t *buf)
{
	sched_t *swalk;
	char *msg, *eoln;

	addtobuffer(buf, "<table summary=\"");
	addtobuffer(buf, htmlquoted(hostname));
	addtobuffer(buf, " scheduled disables\" border=1>\n");

	addtobuffer(buf, "<tr><th>ID</th><th>When</th><th>Command</th><th>&nbsp;</th></tr>\n");
	for (swalk = schedtasks; (swalk); swalk = swalk->next) {
		char idstr[10];

		snprintf(idstr, sizeof(idstr)-1, "%d", swalk->id);
		addtobuffer(buf, "<tr>\n");

		addtobuffer(buf, "<td>");
		addtobuffer(buf, idstr);
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "<td>");
		addtobuffer(buf, ctime(&swalk->when));
		addtobuffer(buf, "</td>\n");

		/* Add an HTML'ized form of the command */
		msg = swalk->cmd; nldecode(msg);
		addtobuffer(buf, "<td>");
		while ((eoln = strchr(msg, '\n')) != NULL) {
			*eoln = '\0';
			addtobuffer(buf, htmlquoted(msg));
			addtobuffer(buf, "<br>");
			msg = (eoln + 1);
		}
		addtobuffer(buf, htmlquoted(msg));
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "<td>");
		addtobuffer(buf, "<form method=\"post\" action=\"");
		addtobuffer(buf, xgetenv("SECURECGIBINURL"));
		addtobuffer(buf, "/enadis.sh\">\n");
		addtobuffer(buf, "<input name=\"hostname\" type=hidden value=\"");
		addtobuffer(buf, htmlquoted(hostname));
		addtobuffer(buf, "\">\n");
		snprintf(idstr, sizeof(idstr), "%d", swalk->id);
		addtobuffer(buf, "<input name=\"canceljob\" type=hidden value=\"");
		addtobuffer(buf, idstr);
		addtobuffer(buf, "\">\n");
		addtobuffer(buf, "<input name=\"go\" type=submit value=\"Cancel\">\n");
		addtobuffer(buf, "</form>\n");
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "</tr>\n");
	}

	addtobuffer(buf, "</table>\n");
}


char *generate_info(char *hostname, char *critconfigfn)
{
	strbuffer_t *infobuf;
	void *hostwalk, *clonewalk;
	char *val;
	int ping, first, gotstatus;
	int alertcolors, alertinterval;

	infobuf = newstrbuffer(0);

	/* Get host info */
	hostwalk = hostinfo(hostname);
	if (!hostwalk) return NULL;

	/* Load alert config */
	alertcolors = colorset(xgetenv("ALERTCOLORS"), ((1 << COL_GREEN) | (1 << COL_BLUE)));
	alertinterval = 60*atoi(xgetenv("ALERTREPEAT"));
	{
		char configfn[PATH_MAX];

		sprintf(configfn, "%s/etc/alerts.cfg", xgetenv("XYMONHOME"));
		load_alertconfig(configfn, alertcolors, alertinterval);
		load_holidays(0);
	}

	/* Load links */
	load_all_links();

	/* Fetch the current host status */
	gotstatus = (fetch_status(hostname) == 0);

	addtobuffer(infobuf, "<table width=\"100%\" summary=\"Host Information\">\n");

	val = xmh_item(hostwalk, XMH_DISPLAYNAME);
	if (val && (strcmp(val, hostname) != 0)) {
		addtobuffer(infobuf, "<tr><th align=left>Hostname:</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, " (");
		addtobuffer(infobuf, htmlquoted(hostname));
		addtobuffer(infobuf, ")</td></tr>\n");
	}
	else {
		addtobuffer(infobuf, "<tr><th align=left>Hostname:</th><td align=left>");
		addtobuffer(infobuf, htmlquoted(hostname));
		addtobuffer(infobuf, "</td></tr>\n");
	}

	val = xmh_item(hostwalk, XMH_CLIENTALIAS);
	if (val && (strcmp(val, hostname) != 0)) {
		addtobuffer(infobuf, "<tr><th align=left>Client alias:</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	if (unametxt) {
		addtobuffer(infobuf, "<tr><th align=left>OS:</th><td align=left>");
		addtobuffer(infobuf, unametxt);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	if (clientvertxt) {
		addtobuffer(infobuf, "<tr><th align=left>Client S/W:</th><td align=left>");
		addtobuffer(infobuf, clientvertxt);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	val = xmh_item(hostwalk, XMH_IP);
	if (conn_null_ip(val)) {
		struct in_addr addr;
		struct hostent *hent;
		static char hostip[50];

		hent = gethostbyname(hostname);
		if (hent) {
			memcpy(&addr, *(hent->h_addr_list), sizeof(struct in_addr));
			strncpy(hostip, inet_ntoa(addr), sizeof(hostip)-1);
			if (inet_aton(hostip, &addr) != 0) {
				strncat(hostip, " (dynamic)", sizeof(hostip)-strlen(hostip)-1);
				val = hostip;
			}
		}
	}
	addtobuffer(infobuf, "<tr><th align=left>IP:</th><td align=left>");
	addtobuffer(infobuf, val);
	addtobuffer(infobuf, "</td></tr>\n");

	val = xmh_item(hostwalk, XMH_DOCURL);
	if (val) {
		addtobuffer(infobuf, "<tr><th align=left>Documentation:</th><td align=left><a href=\"");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "\">");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</a>\n");
	}

	val = hostlink(hostname);
	if (val) {
		addtobuffer(infobuf, "<tr><th align=left>Notes:</th><td align=left><a href=\"");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "\">");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</a>\n");
	}

	val = xmh_item(hostwalk, XMH_PAGEPATH);
	addtobuffer(infobuf, "<tr><th align=left>Page/subpage:</th><td align=left><a href=\"");
	addtobuffer(infobuf, xgetenv("XYMONWEB"));
	addtobuffer(infobuf, "/");
	addtobuffer(infobuf, val);
	addtobuffer(infobuf, "/\">");
	addtobuffer(infobuf, xmh_item(hostwalk, XMH_PAGEPATHTITLE));
	addtobuffer(infobuf, "</a>\n");

	clonewalk = next_host(hostwalk, 1);
	while (clonewalk && (strcmp(hostname, xmh_item(clonewalk, XMH_HOSTNAME)) == 0)) {
		val = xmh_item(clonewalk, XMH_PAGEPATH);
		addtobuffer(infobuf, "<br><a href=\"");
		addtobuffer(infobuf, xgetenv("XYMONWEB"));
		addtobuffer(infobuf, "/");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "/\">");
		addtobuffer(infobuf,xmh_item(clonewalk, XMH_PAGEPATHTITLE));
		addtobuffer(infobuf, "</a>\n");
		clonewalk = next_host(clonewalk, 1);
	}
	addtobuffer(infobuf, "</td></tr>\n");
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	val = xmh_item(hostwalk, XMH_DESCRIPTION);
	if (val) {
		char *delim;

		delim = strchr(val, ':'); if (delim) *delim = '\0';
		addtobuffer(infobuf, "<tr><th align=left>Host type:</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
		if (delim) { 
			*delim = ':'; 
			delim++;
			addtobuffer(infobuf, "<tr><th align=left>Description:</th><td align=left>");
			addtobuffer(infobuf, delim);
			addtobuffer(infobuf, "</td></tr>\n");
		}
		addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");
	}

	if (newcritconfig) {
		/* Load the critical.cfg file and get the alerts for this host */
		int i;
		char istr[10];
		char *key;
		critconf_t *nkrec;
		int firstrec = 1;

		load_critconfig(critconfigfn);
		for (i=0; (i < testcount); i++) {
			key = (char *)malloc(strlen(hostname) + strlen(tnames[i].name) + 2);
			sprintf(key, "%s|%s", hostname, tnames[i].name);
			nkrec = get_critconfig(key, CRITCONF_FIRSTMATCH, NULL);
			if (!nkrec) continue;
			if (firstrec) {
				addtobuffer(infobuf, "<tr><th align=left>Critical alerts:</th>");
				firstrec = 0;
			}
			else {
				addtobuffer(infobuf, "<tr><td>&nbsp;</td>");
			}

			addtobuffer(infobuf, "<td align=left>");
			addtobuffer(infobuf, tnames[i].name);
			addtobuffer(infobuf, ":");

			if (nkrec->crittime && *nkrec->crittime) {
				addtobuffer(infobuf, " ");
				addtobuffer(infobuf, timespec_text(nkrec->crittime));
			}
			else addtobuffer(infobuf, " 24x7");

			sprintf(istr, "%d", nkrec->priority);
			addtobuffer(infobuf, " priority ");
			addtobuffer(infobuf, istr);

			if (nkrec->ttgroup && *nkrec->ttgroup) {
				addtobuffer(infobuf, " resolver group ");
				addtobuffer(infobuf, nkrec->ttgroup);
			}

			addtobuffer(infobuf, "</td></tr>\n");
		}
	}
	else {
		val = xmh_item(hostwalk, XMH_NK);
		if (val) {
			addtobuffer(infobuf, "<tr><th align=left>Critical Alerts:</th><td align=left>");
			addtobuffer(infobuf, val); 

			val = xmh_item(hostwalk, XMH_NKTIME);
			if (val) {
				addtobuffer(infobuf, " (");
				addtobuffer(infobuf, val);
				addtobuffer(infobuf, ")");
			}
			else addtobuffer(infobuf, " (24x7)");

			addtobuffer(infobuf, "</td></tr>\n");
		}
		else {
			addtobuffer(infobuf, "<tr><th align=left>Critical alerts:</th><td align=left>None</td></tr>\n");
		}
	}

	val = xmh_item(hostwalk, XMH_DOWNTIME);
	if (val) {
		char *s = timespec_text(val);
		addtobuffer(infobuf, "<tr><th align=left>Planned downtime:</th><td align=left>");
		addtobuffer(infobuf, s);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	val = xmh_item(hostwalk, XMH_REPORTTIME);
	if (val) {
		char *s = timespec_text(val);
		addtobuffer(infobuf, "<tr><th align=left>SLA report period:</th><td align=left>");
		addtobuffer(infobuf, s);
		addtobuffer(infobuf, "</td></tr>\n");

		val = xmh_item(hostwalk, XMH_WARNPCT);
		if (val == NULL) val = xgetenv("XYMONREPWARN");
		if (val == NULL) val = "(not set)";

		addtobuffer(infobuf, "<tr><th align=left>SLA Availability:</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n"); 
	}

	val = xmh_item(hostwalk, XMH_NOPROPYELLOW);
	if (val) {
		addtobuffer(infobuf, "<tr><th align=left>Suppressed warnings (yellow):</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	val = xmh_item(hostwalk, XMH_NOPROPRED);
	if (val) {
		addtobuffer(infobuf, "<tr><th align=left>Suppressed alarms (red):</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	val = xmh_item(hostwalk, XMH_NOPROPPURPLE);
	if (val) {
		addtobuffer(infobuf, "<tr><th align=left>Suppressed alarms (purple):</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	val = xmh_item(hostwalk, XMH_NOPROPACK);
	if (val) {
		addtobuffer(infobuf, "<tr><th align=left>Suppressed alarms (acked):</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	val = xmh_item(hostwalk, XMH_NET);
	if (val) {
		addtobuffer(infobuf, "<tr><th align=left>Tested from network:</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	if (xmh_item(hostwalk, XMH_FLAG_DIALUP)) {
		addtobuffer(infobuf, "<tr><td colspan=2 align=left>Host downtime does not trigger alarms (dialup host)</td></tr>\n");
	}

	addtobuffer(infobuf, "<tr><th align=left>Network tests use:</th><td align=left>");
	addtobuffer(infobuf, (xmh_item(hostwalk, XMH_FLAG_TESTIP) ? "IP-address" : "Hostname"));
	addtobuffer(infobuf, "</td></tr>\n");

	ping = 1;
	if (xmh_item(hostwalk, XMH_FLAG_NOPING)) ping = 0;
	if (xmh_item(hostwalk, XMH_FLAG_NOCONN)) ping = 0;
	addtobuffer(infobuf, "<tr><th align=left>Checked with ping:</th><td align=left>");
	addtobuffer(infobuf, (ping ? "Yes" : "No"));
	addtobuffer(infobuf, "</td></tr>\n");

	/* Space */
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	first = 1;
	val = xmh_item_walk(hostwalk);
	while (val) {
		if (*val == '~') val++;

		if (strncmp(val, "http", 4) == 0) {
			char *urlstring = decode_url(val, NULL);

			if (first) {
				addtobuffer(infobuf, "<tr><th align=left>URL checks:</th><td align=left>\n");
				first = 0;
			}

			addtobuffer(infobuf, "<a href=\"");
			addtobuffer(infobuf, urlstring);
			addtobuffer(infobuf, "\">");
			addtobuffer(infobuf, htmlquoted(urlstring));
			addtobuffer(infobuf, "</a><br>\n");
		}
		val = xmh_item_walk(NULL);
	}
	if (!first) addtobuffer(infobuf, "</td></tr>\n");

	first = 1;
	val = xmh_item_walk(hostwalk);
	while (val) {
		if (*val == '~') val++;

		if ( (strncmp(val, "cont;", 5) == 0)    ||
		     (strncmp(val, "cont=", 5) == 0)    ||
		     (strncmp(val, "nocont;", 7) == 0)  ||
		     (strncmp(val, "nocont=", 7) == 0)  ||
		     (strncmp(val, "type;", 5) == 0)    ||
		     (strncmp(val, "type=", 5) == 0)    ||
		     (strncmp(val, "post;", 5) == 0)    ||
		     (strncmp(val, "post=", 5) == 0)    ||
		     (strncmp(val, "nopost=", 7) == 0)  ||
		     (strncmp(val, "nopost;", 7) == 0) ) {

			weburl_t weburl;
			char *urlstring = decode_url(val, &weburl);

			if (first) {
				addtobuffer(infobuf, "<tr><th align=left>Content checks:</th><td align=left>\n");
				first = 0;
			}

			addtobuffer(infobuf, "<a href=\"");
			addtobuffer(infobuf, urlstring);
			addtobuffer(infobuf, "\">");
			addtobuffer(infobuf, urlstring);
			addtobuffer(infobuf, "</a>");

			addtobuffer(infobuf, "&nbsp; ");
			addtobuffer(infobuf, ((strncmp(val, "no", 2) == 0) ? "cannot" : "must"));
			addtobuffer(infobuf, " return ");
			addtobuffer(infobuf, ((strncmp(val, "type;", 5) == 0) ? "content-type " : ""));
			addtobuffer(infobuf, "'");
			addtobuffer(infobuf, weburl.expdata);
			addtobuffer(infobuf, "'");
			addtobuffer(infobuf, "<br>\n");
		}

		val = xmh_item_walk(NULL);
	}
	if (!first) addtobuffer(infobuf, "</td></tr>\n");
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	if (!xmh_item(hostwalk, XMH_FLAG_DIALUP)) {
		addtobuffer(infobuf, "<tr><th align=left valign=top>Alerting:</th><td align=left>\n");
		if (gotstatus) 
			generate_xymon_alertinfo(hostname, infobuf);
		else
			addtobuffer(infobuf, "Alert configuration unavailable");
		addtobuffer(infobuf, "</td></tr>\n");
	}
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	addtobuffer(infobuf, "<tr><th align=left valign=top>Holidays</th><td align=left>\n");
	generate_xymon_holidayinfo(hostname, infobuf);
	addtobuffer(infobuf, "</td></tr>\n");
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	if (gotstatus && showenadis) {
		int i, anydisabled = 0;

		generate_xymon_statuslist(hostname, infobuf);
		addtobuffer(infobuf, "<tr><th align=left valign=top>Disable tests</th><td align=left>\n");
		generate_xymon_disable(hostname, infobuf);
		addtobuffer(infobuf, "</td></tr>\n");
		addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		for (i=0; (i < testcount); i++) anydisabled = (anydisabled || (tnames[i].distime != 0));
		if (anydisabled) {
			addtobuffer(infobuf, "<tr><th align=left valign=top>Enable tests</th><td align=left>\n");
			generate_xymon_enable(hostname, infobuf);
			addtobuffer(infobuf, "</td></tr>\n");
			addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");
		}

		if (schedtasks) {
			addtobuffer(infobuf, "<tr><th align=left valign=top>Scheduled tasks</th><td align=left>\n");
			generate_xymon_scheduled(hostname, infobuf);
			addtobuffer(infobuf, "</td></tr>\n");
			addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");
		}
	}

	if (NULL != (val = xmh_item(hostwalk, XMH_DELAYYELLOW))) {
		addtobuffer(infobuf, "<tr><th align=left>Delayed yellow updates:</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}
	if (NULL != (val = xmh_item(hostwalk, XMH_DELAYRED))) {
		addtobuffer(infobuf, "<tr><th align=left>Delayed red updates:</th><td align=left>");
		addtobuffer(infobuf, val);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	addtobuffer(infobuf, "<tr><th align=left>Other tags:</th><td align=left>");
	val = xmh_item_walk(hostwalk);
	while (val) {
		if (*val == '~') val++;

		if ( (xmh_item_idx(val) == -1)          &&
		     (strncmp(val, "http", 4)    != 0)  &&
		     (strncmp(val, "cont;", 5)   != 0)  &&
		     (strncmp(val, "cont=", 5)   != 0)  &&
		     (strncmp(val, "nocont;", 7) != 0)  &&
		     (strncmp(val, "nocont=", 7) != 0)  &&
		     (strncmp(val, "type;", 5)   != 0)  &&
		     (strncmp(val, "type=", 5)   != 0)  &&
		     (strncmp(val, "post;", 5)   != 0)  &&
		     (strncmp(val, "post=", 5)   != 0)  &&
		     (strncmp(val, "nopost=", 7) != 0)  &&
		     (strncmp(val, "nopost;", 7) != 0) ) {
			addtobuffer(infobuf, val);
			addtobuffer(infobuf, " ");
		}

		val = xmh_item_walk(NULL);
	}
	addtobuffer(infobuf, "</td></tr>\n</table>\n");

	return grabstrbuffer(infobuf);
}

