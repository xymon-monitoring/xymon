/*----------------------------------------------------------------------------*/
/* Hobbit "info" column generator.                                            */
/*                                                                            */
/* This is a standalone tool for generating data for the "info" column data.  */
/* This extracts all of the static configuration info about a host contained  */
/* in Hobbit, and displays it on one webpage.                                 */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc-info.c,v 1.108 2006-06-27 12:41:11 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

/* The following is for the DNS lookup we perform on DHCP adresses */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "libbbgen.h"

int showenadis = 1;
int usejsvalidation = 1;
int newnkconfig = 1;

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
	char *hobbitcmd = (char *)malloc(1024 + strlen(hostname));
	char *walk;
	int testsz;
	int haveuname = 0;
	sendreturn_t *sres;

	sres = newsendreturnbuf(1, NULL);
	sprintf(hobbitcmd, "hobbitdboard fields=testname,color,disabletime,dismsg,client,lastchange host=^%s$", hostname);
	if (sendmessage(hobbitcmd, NULL, BBTALK_TIMEOUT, sres) != BB_OK) {
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
		if ( tok && (strcmp(tok, xgetenv("INFOCOLUMN")) != 0) && (strcmp(tok, xgetenv("TRENDSCOLUMN")) != 0) ) {
			tnames[testcount].name = strdup(tok); tok = gettok(NULL, "|"); 
			if (tok) { tnames[testcount].color = parse_color(tok); tok = gettok(NULL, "|"); }
			if (tok) { tnames[testcount].distime = atol(tok); tok = gettok(NULL, "|"); }
			if (tok) { tnames[testcount].dismsg = strdup(tok); tok = gettok(NULL, "|"); }
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


	sprintf(hobbitcmd, "schedule");
	if (sendmessage(hobbitcmd, NULL, BBTALK_TIMEOUT, sres) != BB_OK) {
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
		char *boln, *eoln;

		sprintf(hobbitcmd, "clientlog %s section=uname,osversion,clientversion", hostname);
		if (sendmessage(hobbitcmd, NULL, BBTALK_TIMEOUT, sres) != BB_OK) {
			return 1;
		}
		else {
			clidata = getsendreturnstr(sres, 1);
		}

		boln = strstr(clidata, "[osversion]\n");
		if (boln) {
			boln = strchr(boln, '\n') + 1;
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			unametxt = strdup(boln);
			if (eoln) *eoln = '\n';
		}

		boln = strstr(clidata, "[uname]\n");
		if (boln) {
			boln = strchr(boln, '\n') + 1;
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			if (unametxt) {
				unametxt = (char *)realloc(unametxt, strlen(unametxt) + strlen(boln) + 6);
				strcat(unametxt, "<br>\n");
				strcat(unametxt, boln);
			}
			else {
				unametxt = strdup(boln);
			}
			if (eoln) *eoln = '\n';
		}

		boln = strstr(clidata, "[clientversion]\n");
		if (boln) {
			boln = strchr(boln, '\n') + 1;
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			clientvertxt = strdup(boln);
			if (eoln) *eoln = '\n';
		}

		xfree(clidata);
	}

	freesendreturnbuf(sres);

	return 0;
}

static void generate_hobbit_alertinfo(char *hostname, strbuffer_t *buf)
{
	void *hi = hostinfo(hostname);
	activealerts_t *alert;
	char l[1024];
	int i, rcount;

	sprintf(l, "<table summary=\"%s Alerts\" border=1>\n", hostname);
	addtobuffer(buf, l);
	addtobuffer(buf, "<tr><th>Service</th><th>Recipient</th><th>1st Delay</th><th>Stop after</th><th>Repeat</th><th>Time of Day</th><th>Colors</th></tr>\n");

	alert = calloc(1, sizeof(activealerts_t));
	alert->hostname = hostname;
	alert->location = (hi ? bbh_item(hi, BBH_ALLPAGEPATHS) : "");
	strcpy(alert->ip, "127.0.0.1");
	alert->color = COL_RED;
	alert->pagemessage = "";
	alert->state = A_PAGING;
	alert->cookie = 12345;
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

static void generate_hobbit_holidayinfo(char *hostname, strbuffer_t *buf)
{
	void *hi = hostinfo(hostname);
	char l[1024];
	time_t now = getcurrenttime(NULL);
	struct tm *tm;
	int month, year;
	int needreload = 0;
	char *holidayset;

	tm = localtime(&now);
	year = tm->tm_year + 1900;
	month = tm->tm_mon;

	holidayset = bbh_item(hi, BBH_HOLIDAYS);

	sprintf(l, "<table summary=\"%s Holidays\" border=1>\n", hostname);
	addtobuffer(buf, l);

	addtobuffer(buf, "<tr>");

	/* In January+February, show last year's holidays */
	if (month <= 1) {
		needreload = 1;
		load_holidays(year-1);
		addtobuffer(buf, "<td><table>\n");
		if (holidayset) {
			sprintf(l, "<tr><th colspan=2>Holidays %d (%s)</th></tr>\n", year-1, holidayset);
		}
		else {
			sprintf(l, "<tr><th colspan=2>Holidays %d</th></tr>\n", year-1);
		}
		addtobuffer(buf, l);
		printholidays(holidayset, buf);
		addtobuffer(buf, "</table></td>\n");
	}

	/* Show this year's holidays */
	if (needreload) load_holidays(year);
	addtobuffer(buf, "<td><table>\n");
	if (holidayset) {
		sprintf(l, "<tr><th colspan=2>Holidays %d (%s)</th></tr>\n", year, holidayset);
	}
	else {
		sprintf(l, "<tr><th colspan=2>Holidays %d</th></tr>\n", year);
	}
	addtobuffer(buf, l);
	printholidays(holidayset, buf);
	addtobuffer(buf, "</table></td>\n");

	/* In November+December, show next year's holidays */
	if (month >= 10) {
		needreload = 1;
		load_holidays(year+1);
		addtobuffer(buf, "<td><table>\n");
		if (holidayset) {
			sprintf(l, "<tr><th colspan=2>Holidays %d (%s)</th></tr>\n", year+1, holidayset);
		}
		else {
			sprintf(l, "<tr><th colspan=2>Holidays %d</th></tr>\n", year+1);
		}
		addtobuffer(buf, l);
		printholidays(holidayset, buf);
		addtobuffer(buf, "</table></td>\n");
	}

	addtobuffer(buf, "</tr>\n");

	addtobuffer(buf, "</table>\n");

	if (needreload) load_holidays(0);
}



static void generate_hobbit_statuslist(char *hostname, strbuffer_t *buf)
{
	char msgline[4096];
	char datestr[100];
	int i, btncount;
	char *bbdatefmt;
	strbuffer_t *servRed, *servYellow, *servPurple, *servBlue;
	time_t logage;

	bbdatefmt = xgetenv("BBDATEFORMAT");

	servRed = newstrbuffer(0);
	servYellow = newstrbuffer(0);
	servPurple = newstrbuffer(0);
	servBlue = newstrbuffer(0);

	addtobuffer(buf, "<tr><th align=left valign=top>Status summary</th><td align=left>\n");
	addtobuffer(buf, "<form name=\"colorsel\" action=\"nosubmit\" method=\"GET\">\n");
	addtobuffer(buf, "<table summary=\"Status summary\" border=1>\n");
	addtobuffer(buf, "<tr><th>Service</th><th>Since</th><th>Duration</th></tr>\n");

	for (i = 0; i < testcount; i++) {
		strftime(datestr, sizeof(datestr), bbdatefmt, localtime(&tnames[i].lastchange));
		logage = getcurrenttime(NULL) - tnames[i].lastchange;

		addtobuffer(buf, "<tr>");

		sprintf(msgline, "<td><img src=\"%s/%s\" height=\"%s\" width=\"%s\" border=0 alt=\"%s status\"> %s</td>",
			xgetenv("BBSKIN"), dotgiffilename(tnames[i].color, 0, 1),
			xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"),
			colorname(tnames[i].color), tnames[i].name);
		addtobuffer(buf, msgline);

		sprintf(msgline, "<td>%s</td>", datestr);
		addtobuffer(buf, msgline);

		sprintf(msgline, "<td align=right>%d days, %02d hours, %02d minutes</td>",
			(int)(logage / 86400),(int) ((logage % 86400) / 3600),(int) ((logage % 3600) / 60));
		addtobuffer(buf, msgline);

		addtobuffer(buf, "</tr>\n");

		sprintf(msgline, ",%s", tnames[i].name);
		switch (tnames[i].color) {
		  case COL_BLUE   : addtobuffer(servBlue, msgline);   break;
		  case COL_RED    : addtobuffer(servRed, msgline);    break;
		  case COL_YELLOW : addtobuffer(servYellow, msgline); break;
		  case COL_PURPLE : addtobuffer(servPurple, msgline); break;
		}
	}

	btncount = 0;
	if (STRBUFLEN(servRed) > 0)    btncount++;
	if (STRBUFLEN(servYellow) > 0) btncount++;
	if (STRBUFLEN(servPurple) > 0) btncount++;
	if (STRBUFLEN(servBlue) > 0)   btncount++;
	if (btncount > 0) {
		addtobuffer(buf, "<tr><td colspan=3>\n");

		addtobuffer(buf, "<table width=\"100%\">\n");
		sprintf(msgline, "<tr><th colspan=%d><center><i>Toggle tests to disable</i></center></th></tr>\n", btncount);
		addtobuffer(buf, msgline);

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

static void generate_hobbit_disable(char *hostname, strbuffer_t *buf)
{
	int i;
	char l[1024];
	time_t now = getcurrenttime(NULL);
	int beginyear, endyear;
	struct tm monthtm;
	struct tm *nowtm;
	char mname[20];
	char *selstr;

	nowtm = localtime(&now);
	beginyear = nowtm->tm_year + 1900;
	endyear = nowtm->tm_year + 1900 + 5;

	sprintf(l, "<form name=\"disableform\" method=\"post\" action=\"%s/hobbit-enadis.sh\">\n", xgetenv("SECURECGIBINURL"));
	addtobuffer(buf, l);
	sprintf(l, "<table summary=\"%s disable\" border=1>\n", hostname);
	addtobuffer(buf, l);

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

		sprintf(l, "<option value=\"%s\" style=\"%s\">%s</option>\n", 
			tnames[i].name, colstyle, tnames[i].name);
		addtobuffer(buf, l);
	}
	addtobuffer(buf, "</select></td>\n");

	addtobuffer(buf, "<td>\n");
	addtobuffer(buf, "   <table summary=\"Disable parameters\" border=0>\n");
	addtobuffer(buf, "      <tr> <td>Cause: <input name=\"cause\" type=text size=50 maxlength=80></td> </tr>\n");

	addtobuffer(buf, "      <tr>\n");
	addtobuffer(buf, "        <td>Duration: <input name=\"duration\" type=text size=5 maxlength=5 value=\"4\"> &nbsp;\n");
	addtobuffer(buf, "            <select name=\"scale\">\n");
	addtobuffer(buf, "               <option value=1>minutes</option>\n");
	addtobuffer(buf, "               <option value=60 selected>hours</option>\n");
	addtobuffer(buf, "               <option value=1440>days</option>\n");
	addtobuffer(buf, "               <option value=10080>weeks</option>\n");
	addtobuffer(buf, "            </select>\n");
	addtobuffer(buf, "            &nbsp;&nbsp;-&nbsp;OR&nbsp;-&nbsp;until&nbsp;OK:<input name=\"untilok\" type=checkbox>");
	addtobuffer(buf, "        </td>\n");
	addtobuffer(buf, "      </tr>\n");

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
		if (i == (nowtm->tm_mon + 1)) selstr = "SELECTED"; else selstr = "";
		monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
		monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
		strftime(mname, sizeof(mname)-1, "%B", &monthtm);
		sprintf(l, "<OPTION VALUE=\"%d\" %s>%s</OPTION>\n", i, selstr, mname);
		addtobuffer(buf, l);
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Days */
	addtobuffer(buf, "<SELECT NAME=\"day\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=1; (i <= 31); i++) {
		if (i == nowtm->tm_mday) selstr = "SELECTED"; else selstr = "";
		sprintf(l, "<OPTION VALUE=\"%d\" %s>%d</OPTION>\n", i, selstr, i);
		addtobuffer(buf, l);
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Years */
	addtobuffer(buf, "<SELECT NAME=\"year\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=beginyear; (i <= endyear); i++) {
		if (i == (nowtm->tm_year + 1900)) selstr = "SELECTED"; else selstr = "";
		sprintf(l, "<OPTION VALUE=\"%d\" %s>%d</OPTION>\n", i, selstr, i);
		addtobuffer(buf, l);
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Hours */
	addtobuffer(buf, "<SELECT NAME=\"hour\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=0; (i <= 24); i++) {
		if (i == nowtm->tm_hour) selstr = "SELECTED"; else selstr = "";
		sprintf(l, "<OPTION VALUE=\"%d\" %s>%d</OPTION>\n", i, selstr, i);
		addtobuffer(buf, l);
	}
	addtobuffer(buf, "</SELECT>\n");

	/* Minutes */
	addtobuffer(buf, "<SELECT NAME=\"minute\" onClick=\"setcheck(this.form.go,true)\">\n");
	for (i=0; (i <= 59); i++) {
		if (i == nowtm->tm_min) selstr = "SELECTED"; else selstr = "";
		sprintf(l, "<OPTION VALUE=\"%02d\" %s>%02d</OPTION>\n", i, selstr, i);
		addtobuffer(buf, l);
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

	sprintf(l, "<input name=\"hostname\" type=hidden value=\"%s\">\n", hostname);
	addtobuffer(buf, l);
	addtobuffer(buf, "</form>\n");
}

static void generate_hobbit_enable(char *hostname, strbuffer_t *buf)
{
	int i;
	char l[1024];
	char *msg, *eoln;

	sprintf(l, "<table summary=\"%s disabled tests\" border=1>\n", hostname);
	addtobuffer(buf, l);

	addtobuffer(buf, "<tr><th>Test</th><th>Disabled until</th><th>Cause</th><th>&nbsp;</th></tr>\n");

	for (i=0; (i < testcount); i++) {
		if (tnames[i].distime == 0) continue;

		addtobuffer(buf, "<tr>\n");

		sprintf(l, "<td>%s</td>\n", tnames[i].name);
		addtobuffer(buf, l);
		sprintf(l, "<td>%s</td>\n", 
			(tnames[i].distime == -1) ? "OK" : ctime(&tnames[i].distime));
		addtobuffer(buf, l);

		/* Add an HTML'ized form of the disable-message */
		msg = tnames[i].dismsg; nldecode(msg); msg += strspn(msg, "0123456789 \t\n");
		addtobuffer(buf, "<td>");
		while ((eoln = strchr(msg, '\n')) != NULL) {
			*eoln = '\0';
			addtobuffer(buf, msg);
			addtobuffer(buf, "<br>");
			msg = (eoln + 1);
		}
		addtobuffer(buf, msg);
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "<td>");
		sprintf(l, "<form method=\"post\" action=\"%s/hobbit-enadis.sh\">\n", xgetenv("SECURECGIBINURL"));
		addtobuffer(buf, l);
		sprintf(l, "<input name=\"hostname\" type=hidden value=\"%s\">\n", hostname);
		addtobuffer(buf, l);
		sprintf(l, "<input name=\"enabletest\" type=hidden value=\"%s\">\n", tnames[i].name);
		addtobuffer(buf, l);
		addtobuffer(buf, "<input name=\"go\" type=submit value=\"Enable\">\n");
		addtobuffer(buf, "</form>\n");
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "</tr>\n");
	}

	addtobuffer(buf, "<tr><td>ALL</td><td>&nbsp;</td><td>&nbsp;</td>\n");

	addtobuffer(buf, "<td>");
	sprintf(l, "<form method=\"post\" action=\"%s/hobbit-enadis.sh\">\n", xgetenv("SECURECGIBINURL"));
	addtobuffer(buf, l);
	sprintf(l, "<input name=\"hostname\" type=hidden value=\"%s\">\n", hostname);
	addtobuffer(buf, l);
	sprintf(l, "<input name=\"enabletest\" type=hidden value=\"%s\">\n", "*");
	addtobuffer(buf, l);
	addtobuffer(buf, "<input name=\"go\" type=submit value=\"Enable\">\n");
	addtobuffer(buf, "</form>\n");
	addtobuffer(buf, "</td>\n");

	addtobuffer(buf, "</tr>\n");

	addtobuffer(buf, "</table>\n");
}


static void generate_hobbit_scheduled(char *hostname, strbuffer_t *buf)
{
	char l[1024];
	sched_t *swalk;
	char *msg, *eoln;

	sprintf(l, "<table summary=\"%s scheduled disables\" border=1>\n", hostname);
	addtobuffer(buf, l);

	addtobuffer(buf, "<tr><th>ID</th><th>When</th><th>Command</th><th>&nbsp;</th></tr>\n");
	for (swalk = schedtasks; (swalk); swalk = swalk->next) {
		addtobuffer(buf, "<tr>\n");

		sprintf(l, "<td>%d</td>\n", swalk->id);
		addtobuffer(buf, l);

		sprintf(l, "<td>%s</td>\n", ctime(&swalk->when));
		addtobuffer(buf, l);

		/* Add an HTML'ized form of the command */
		msg = swalk->cmd; nldecode(msg);
		addtobuffer(buf, "<td>");
		while ((eoln = strchr(msg, '\n')) != NULL) {
			*eoln = '\0';
			addtobuffer(buf, msg);
			addtobuffer(buf, "<br>");
			msg = (eoln + 1);
		}
		addtobuffer(buf, msg);
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "<td>");
		sprintf(l, "<form method=\"post\" action=\"%s/hobbit-enadis.sh\">\n", xgetenv("SECURECGIBINURL"));
		addtobuffer(buf, l);
		sprintf(l, "<input name=\"hostname\" type=hidden value=\"%s\">\n", hostname);
		addtobuffer(buf, l);
		sprintf(l, "<input name=\"canceljob\" type=hidden value=\"%d\">\n", swalk->id);
		addtobuffer(buf, l);
		addtobuffer(buf, "<input name=\"go\" type=submit value=\"Cancel\">\n");
		addtobuffer(buf, "</form>\n");
		addtobuffer(buf, "</td>\n");

		addtobuffer(buf, "</tr>\n");
	}

	addtobuffer(buf, "</table>\n");
}


char *generate_info(char *hostname)
{
	strbuffer_t *infobuf;
	char l[MAX_LINE_LEN];
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

		sprintf(configfn, "%s/etc/hobbit-alerts.cfg", xgetenv("BBHOME"));
		load_alertconfig(configfn, alertcolors, alertinterval);
		load_holidays(0);
	}

	/* Load links */
	load_all_links();

	/* Fetch the current host status */
	gotstatus = (fetch_status(hostname) == 0);

	addtobuffer(infobuf, "<table width=\"100%\" summary=\"Host Information\">\n");

	val = bbh_item(hostwalk, BBH_DISPLAYNAME);
	if (val && (strcmp(val, hostname) != 0)) {
		sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s (%s)</td></tr>\n", 
			val, hostname);
	}
	else {
		sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s</td></tr>\n", hostname);
	}
	addtobuffer(infobuf, l);

	val = bbh_item(hostwalk, BBH_CLIENTALIAS);
	if (val && (strcmp(val, hostname) != 0)) {
		sprintf(l, "<tr><th align=left>Client alias:</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(infobuf, l);
	}

	if (unametxt) {
		sprintf(l, "<tr><th align=left>OS:</th><td align=left>%s</td></tr>\n", unametxt);
		addtobuffer(infobuf, l);
	}

	if (clientvertxt) {
		sprintf(l, "<tr><th align=left>Client S/W:</th><td align=left>%s</td></tr>\n", clientvertxt);
		addtobuffer(infobuf, l);
	}

	val = bbh_item(hostwalk, BBH_IP);
	if (strcmp(val, "0.0.0.0") == 0) {
		struct in_addr addr;
		struct hostent *hent;
		static char hostip[IP_ADDR_STRLEN + 20];

		hent = gethostbyname(hostname);
		if (hent) {
			memcpy(&addr, *(hent->h_addr_list), sizeof(struct in_addr));
			strcpy(hostip, inet_ntoa(addr));
			if (inet_aton(hostip, &addr) != 0) {
				strcat(hostip, " (dynamic)");
				val = hostip;
			}
		}
	}
	sprintf(l, "<tr><th align=left>IP:</th><td align=left>%s</td></tr>\n", val);
	addtobuffer(infobuf, l);

	val = bbh_item(hostwalk, BBH_DOCURL);
	if (val) {
		sprintf(l, "<tr><th align=left>Documentation:</th><td align=left><a href=\"%s\">%s</a>\n", val, val);
		addtobuffer(infobuf, l);
	}

	val = hostlink(hostname);
	if (val) {
		sprintf(l, "<tr><th align=left>Notes:</th><td align=left><a href=\"%s\">%s%s</a>\n", 
			val, xgetenv("BBWEBHOST"), val);
		addtobuffer(infobuf, l);
	}

	val = bbh_item(hostwalk, BBH_PAGEPATH);
	sprintf(l, "<tr><th align=left>Page/subpage:</th><td align=left><a href=\"%s/%s/\">%s</a>\n", 
		xgetenv("BBWEB"), val, bbh_item(hostwalk, BBH_PAGEPATHTITLE));
	addtobuffer(infobuf, l);

	clonewalk = next_host(hostwalk, 1);
	while (clonewalk && (strcmp(hostname, bbh_item(clonewalk, BBH_HOSTNAME)) == 0)) {
		val = bbh_item(clonewalk, BBH_PAGEPATH);
		sprintf(l, "<br><a href=\"%s/%s/\">%s</a>\n", 
			xgetenv("BBWEB"), val, bbh_item(clonewalk, BBH_PAGEPATHTITLE));
		addtobuffer(infobuf, l);
		clonewalk = next_host(clonewalk, 1);
	}
	addtobuffer(infobuf, "</td></tr>\n");
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	val = bbh_item(hostwalk, BBH_DESCRIPTION);
	if (val) {
		char *delim;

		delim = strchr(val, ':'); if (delim) *delim = '\0';
		sprintf(l, "<tr><th align=left>Host type:</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(infobuf, l);
		if (delim) { 
			*delim = ':'; 
			delim++;
			sprintf(l, "<tr><th align=left>Description:</th><td align=left>%s</td></tr>\n", delim);
			addtobuffer(infobuf, l);
		}
		addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");
	}

	if (newnkconfig) {
		/* Load the hobbit-nkview.cfg file and get the alerts for this host */
		int i;
		char *key;
		nkconf_t *nkrec;
		int firstrec = 1;

		load_nkconfig(NULL);
		for (i=0; (i < testcount); i++) {
			key = (char *)malloc(strlen(hostname) + strlen(tnames[i].name) + 2);
			sprintf(key, "%s|%s", hostname, tnames[i].name);
			nkrec = get_nkconfig(key, NKCONF_FIRSTMATCH, NULL);
			if (!nkrec) continue;
			if (firstrec) {
				addtobuffer(infobuf, "<tr><th align=left>NK alerts:</th>");
				firstrec = 0;
			}
			else {
				addtobuffer(infobuf, "<tr><td>&nbsp;</td>");
			}

			sprintf(l, "<td align=left>%s:", tnames[i].name);
			addtobuffer(infobuf, l);

			if (nkrec->nktime && *nkrec->nktime) {
				sprintf(l, " %s", timespec_text(nkrec->nktime));
				addtobuffer(infobuf, l);
			}
			else addtobuffer(infobuf, " 24x7");

			sprintf(l, " priority %d", nkrec->priority);
			addtobuffer(infobuf, l);

			if (nkrec->ttgroup && *nkrec->ttgroup) {
				sprintf(l, " resolver group %s", nkrec->ttgroup);
				addtobuffer(infobuf, l);
			}

			addtobuffer(infobuf, "</td></tr>\n");
		}
	}
	else {
		val = bbh_item(hostwalk, BBH_NK);
		if (val) {
			sprintf(l, "<tr><th align=left>NK Alerts:</th><td align=left>%s", val); 
			addtobuffer(infobuf, l);

			val = bbh_item(hostwalk, BBH_NKTIME);
			if (val) {
				sprintf(l, " (%s)", val);
				addtobuffer(infobuf, l);
			}
			else addtobuffer(infobuf, " (24x7)");

			addtobuffer(infobuf, "</td></tr>\n");
		}
		else {
			addtobuffer(infobuf, "<tr><th align=left>NK alerts:</th><td align=left>None</td></tr>\n");
		}
	}

	val = bbh_item(hostwalk, BBH_DOWNTIME);
	if (val) {
		char *s = timespec_text(val);
		addtobuffer(infobuf, "<tr><th align=left>Planned downtime:</th><td align=left>");
		addtobuffer(infobuf, s);
		addtobuffer(infobuf, "</td></tr>\n");
	}

	val = bbh_item(hostwalk, BBH_REPORTTIME);
	if (val) {
		char *s = timespec_text(val);
		addtobuffer(infobuf, "<tr><th align=left>SLA report period:</th><td align=left>");
		addtobuffer(infobuf, s);
		addtobuffer(infobuf, "</td></tr>\n");

		val = bbh_item(hostwalk, BBH_WARNPCT);
		if (val == NULL) val = xgetenv("BBREPWARN");
		if (val == NULL) val = "(not set)";

		sprintf(l, "<tr><th align=left>SLA Availability:</th><td align=left>%s</td></tr>\n", val); 
		addtobuffer(infobuf, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPYELLOW);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed warnings (yellow):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(infobuf, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPRED);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed alarms (red):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(infobuf, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPPURPLE);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed alarms (purple):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(infobuf, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPACK);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed alarms (acked):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(infobuf, l);
	}
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	val = bbh_item(hostwalk, BBH_NET);
	if (val) {
		sprintf(l, "<tr><th align=left>Tested from network:</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(infobuf, l);
	}

	if (bbh_item(hostwalk, BBH_FLAG_DIALUP)) {
		addtobuffer(infobuf, "<tr><td colspan=2 align=left>Host downtime does not trigger alarms (dialup host)</td></tr>\n");
	}

	sprintf(l, "<tr><th align=left>Network tests use:</th><td align=left>%s</td></tr>\n", 
		(bbh_item(hostwalk, BBH_FLAG_TESTIP) ? "IP-address" : "Hostname"));
	addtobuffer(infobuf, l);

	ping = 1;
	if (bbh_item(hostwalk, BBH_FLAG_NOPING)) ping = 0;
	if (bbh_item(hostwalk, BBH_FLAG_NOCONN)) ping = 0;
	sprintf(l, "<tr><th align=left>Checked with ping:</th><td align=left>%s</td></tr>\n", (ping ? "Yes" : "No"));
	addtobuffer(infobuf, l);

	/* Space */
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	first = 1;
	val = bbh_item_walk(hostwalk);
	while (val) {
		if (*val == '~') val++;

		if (strncmp(val, "http", 4) == 0) {
			char *urlstring = decode_url(val, NULL);

			if (first) {
				addtobuffer(infobuf, "<tr><th align=left>URL checks:</th><td align=left>\n");
				first = 0;
			}

			sprintf(l, "<a href=\"%s\">%s</a><br>\n", urlstring, urlstring);
			addtobuffer(infobuf, l);
		}
		val = bbh_item_walk(NULL);
	}
	if (!first) addtobuffer(infobuf, "</td></tr>\n");

	first = 1;
	val = bbh_item_walk(hostwalk);
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

			bburl_t bburl;
			char *urlstring = decode_url(val, &bburl);

			if (first) {
				addtobuffer(infobuf, "<tr><th align=left>Content checks:</th><td align=left>\n");
				first = 0;
			}

			sprintf(l, "<a href=\"%s\">%s</a>", urlstring, urlstring);
			addtobuffer(infobuf, l);

			sprintf(l, "&nbsp; %s return %s'%s'", 
					((strncmp(val, "no", 2) == 0) ? "cannot" : "must"), 
					((strncmp(val, "type;", 5) == 0) ? "content-type " : ""),
					bburl.expdata);
			addtobuffer(infobuf, l);
			addtobuffer(infobuf, "<br>\n");
		}

		val = bbh_item_walk(NULL);
	}
	if (!first) addtobuffer(infobuf, "</td></tr>\n");
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	if (!bbh_item(hostwalk, BBH_FLAG_DIALUP)) {
		addtobuffer(infobuf, "<tr><th align=left valign=top>Alerting:</th><td align=left>\n");
		if (gotstatus) 
			generate_hobbit_alertinfo(hostname, infobuf);
		else
			addtobuffer(infobuf, "Alert configuration unavailable");
		addtobuffer(infobuf, "</td></tr>\n");
	}
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	addtobuffer(infobuf, "<tr><th align=left valign=top>Holidays</th><td align=left>\n");
	generate_hobbit_holidayinfo(hostname, infobuf);
	addtobuffer(infobuf, "</td></tr>\n");
	addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	if (gotstatus && showenadis) {
		int i, anydisabled = 0;

		generate_hobbit_statuslist(hostname, infobuf);
		addtobuffer(infobuf, "<tr><th align=left valign=top>Disable tests</th><td align=left>\n");
		generate_hobbit_disable(hostname, infobuf);
		addtobuffer(infobuf, "</td></tr>\n");
		addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		for (i=0; (i < testcount); i++) anydisabled = (anydisabled || (tnames[i].distime != 0));
		if (anydisabled) {
			addtobuffer(infobuf, "<tr><th align=left valign=top>Enable tests</th><td align=left>\n");
			generate_hobbit_enable(hostname, infobuf);
			addtobuffer(infobuf, "</td></tr>\n");
			addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");
		}

		if (schedtasks) {
			addtobuffer(infobuf, "<tr><th align=left valign=top>Scheduled tasks</th><td align=left>\n");
			generate_hobbit_scheduled(hostname, infobuf);
			addtobuffer(infobuf, "</td></tr>\n");
			addtobuffer(infobuf, "<tr><td colspan=2>&nbsp;</td></tr>\n");
		}
	}

	addtobuffer(infobuf, "<tr><th align=left>Other tags:</th><td align=left>");
	val = bbh_item_walk(hostwalk);
	while (val) {
		if (*val == '~') val++;

		if ( (bbh_item_idx(val) == -1)          &&
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
			sprintf(l, "%s ", val);
			addtobuffer(infobuf, l);
		}

		val = bbh_item_walk(NULL);
	}
	addtobuffer(infobuf, "</td></tr>\n</table>\n");

	return grabstrbuffer(infobuf);
}

