/*----------------------------------------------------------------------------*/
/* Xymon alert acknowledgment CGI tool.                                       */
/*                                                                            */
/* This is a CGI script for handling acknowledgments of alerts.               */
/* If called with no CGI query, it will present the acknowledgment form;      */
/* if called with a proper CGI query string it will send an ack-message to    */
/* the Xymon daemon.                                                          */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libxymon.h"
#include "version.h"

static cgidata_t *cgidata = NULL;
static int  nopin = 0;

typedef struct acklist_t {
	int  id, checked;
	int acknum;
	int validity;
	char *hostname;
	char *testname;
	char *ackmsg;
	struct acklist_t *next;
} acklist_t;
acklist_t *ackhead = NULL;
acklist_t *acktail = NULL;
char *validityall = NULL;
char *ackmsgall = NULL;
enum { ACK_UNKNOWN, ACK_OLDSTYLE, ACK_ONE, ACK_MANY } reqtype = ACK_UNKNOWN;
int sendnum = 0;

static void parse_query(void)
{
	cgidata_t *cwalk;

	/* See what kind of request this is */
	for (cwalk=cgidata; (cwalk); cwalk = cwalk->next) {
		if (nopin && (strcmp(cwalk->name, "Send_all") == 0)) {
			/* User pushed the "Send all" button */
			reqtype = ACK_MANY;
		}
		else if (nopin && (strncmp(cwalk->name, "Send_", 5) == 0)) {
			/* User pushed a specific "Send" button */
			sendnum = atoi(cwalk->name+5);
			reqtype = ACK_ONE;
		}
		else if (!nopin && (strcmp(cwalk->name, "Send") == 0)) {
			/* Old style request */
			reqtype = ACK_OLDSTYLE;
		}
	}

	for (cwalk=cgidata; (cwalk); cwalk = cwalk->next) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */
		int id = 0;
		char *acknum = NULL, *validity = NULL, *ackmsg = NULL;
		char *hostname = NULL, *testname = NULL, *checked = NULL;
		char *delim;

		if (strncasecmp(cwalk->name, "NUMBER", 6) == 0) {
			if (*cwalk->value) acknum = cwalk->value;
			delim = strchr(cwalk->name, '_'); if (delim) id = atoi(delim+1);
		}
		else if (strcasecmp(cwalk->name, "DELAY_all") == 0) {
			if (*cwalk->value) validityall = cwalk->value;
		}
		else if (strcasecmp(cwalk->name, "MESSAGE_all") == 0) {
			if (*cwalk->value) ackmsgall = cwalk->value;
		}
		else if (strncasecmp(cwalk->name, "DELAY", 5) == 0) {
			if (*cwalk->value) validity = cwalk->value;
			delim = strchr(cwalk->name, '_'); if (delim) id = atoi(delim+1);
		}
		else if (strncasecmp(cwalk->name, "MESSAGE", 7) == 0) {
			if (*cwalk->value) ackmsg = cwalk->value;
			delim = strchr(cwalk->name, '_'); if (delim) id = atoi(delim+1);
		}
		else if (strncasecmp(cwalk->name, "HOSTNAME", 8) == 0) {
			if (*cwalk->value) hostname = cwalk->value;
			delim = strchr(cwalk->name, '_'); if (delim) id = atoi(delim+1);
		}
		else if (strncasecmp(cwalk->name, "TESTNAME", 8) == 0) {
			if (*cwalk->value) testname = cwalk->value;
			delim = strchr(cwalk->name, '_'); if (delim) id = atoi(delim+1);
		}
		else if (strncasecmp(cwalk->name, "CHECKED", 7) == 0) {
			if (*cwalk->value) checked = cwalk->value;
			delim = strchr(cwalk->name, '_'); if (delim) id = atoi(delim+1);
		}

		switch (reqtype) {
		  case ACK_UNKNOWN:
			break;

		  case ACK_OLDSTYLE:
			id = 1;
			/* Fall through */
		  case ACK_ONE:
			if ((id == sendnum) || (reqtype == ACK_OLDSTYLE)) checked = "checked";
			/* Fall through */
		  case ACK_MANY:
			if (id) {
				acklist_t *awalk;

				awalk = ackhead; while (awalk && (awalk->id != id)) awalk = awalk->next;
				if (!awalk) {
					awalk = (acklist_t *)calloc(1, sizeof(acklist_t));
					awalk->id = id;
					awalk->next = NULL;

					if (!ackhead) ackhead = acktail = awalk;
					else { acktail->next = awalk; acktail = awalk; }
				}

				if (acknum) awalk->acknum = atoi(acknum);
				if (validity) awalk->validity = durationvalue(validity);
				if (ackmsg) awalk->ackmsg = strdup(ackmsg);
				if (hostname) awalk->hostname = strdup(hostname);
				if (testname) awalk->testname = strdup(testname);
				if (checked) awalk->checked = 1;
			}
			break;
		}
	}
}

void generate_ackline(FILE *output, char *hname, char *tname, char *ackcode)
{
	static int num = 0;
	char numstr[10];

	num++;
	if (ackcode) {
		sprintf(numstr, "%d", num); 
	}
	else {
		strcpy(numstr, "all");
	}

	fprintf(output, "<tr>\n");

	fprintf(output, "    <td align=left>%s</td>\n", (hname ? htmlquoted(hname) : "&nbsp;"));
	fprintf(output, "    <td align=left>%s</td>\n", (tname ? htmlquoted(tname) : "&nbsp;"));
	fprintf(output, "    <TD><INPUT TYPE=TEXT NAME=\"DELAY_%s\" SIZE=8 MAXLENGTH=20></TD>\n", numstr);
	fprintf(output, "    <TD><INPUT TYPE=TEXT NAME=\"MESSAGE_%s\" SIZE=60 MAXLENGTH=80></TD>\n", numstr);

	fprintf(output, "    <TD>\n");
	if (ackcode && hname && tname) {
		fprintf(output, "       <INPUT TYPE=\"HIDDEN\" NAME=\"NUMBER_%d\" VALUE=\"%s\">\n", num, htmlquoted(ackcode));
		fprintf(output, "       <INPUT TYPE=\"HIDDEN\" NAME=\"HOSTNAME_%d\" VALUE=\"%s\">\n", num, htmlquoted(hname));
		fprintf(output, "       <INPUT TYPE=\"HIDDEN\" NAME=\"TESTNAME_%d\" VALUE=\"%s\">\n", num, htmlquoted(tname));
		fprintf(output, "       <INPUT TYPE=\"SUBMIT\" NAME=\"Send_%d\" VALUE=\"Send\" ALT=\"Send\">\n", num);
	}
	else {
		fprintf(output, "       &nbsp;\n");
	}
	fprintf(output, "    </TD>\n");

	fprintf(output, "    <TD>\n");
	if (ackcode) fprintf(output, "       <INPUT TYPE=\"CHECKBOX\" NAME=\"CHECKED_%d\" VALUE=\"OFF\">\n", num);
	else         fprintf(output, "       <INPUT TYPE=\"SUBMIT\" NAME=\"Send_all\" VALUE=\"Send\" ALT=\"Send\">\n");
	fprintf(output, "    </TD>\n");

	fprintf(output, "</tr>\n");
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	int obeycookies = 1;

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
		else if (strcmp(argv[argi], "--no-pin") == 0) {
			nopin = 1;
		}
		else if (strcmp(argv[argi], "--no-cookies") == 0) {
			obeycookies = 0;
		}
	}

	redirect_cgilog("ack");

	cgidata = cgi_request();
	if ( (nopin && (cgi_method == CGI_GET)) || (!nopin && (cgidata == NULL)) ) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_RED), NULL);

		printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

		if (!nopin) {
			showform(stdout, "acknowledge", "acknowledge_form", COL_RED, getcurrenttime(NULL), 
				 NULL, NULL);
		}
		else {
			char *cmd;
			char *respbuf = NULL;
			char *hostname, *pagename;
			int gotfilter = 0;
			sendreturn_t *sres = NULL;

			headfoot(stdout, "acknowledge", "", "header", COL_RED);

			cmd = (char *)malloc(1024);
			strcpy(cmd, "xymondboard color=red,yellow fields=hostname,testname,cookie");

			if (obeycookies && !gotfilter && ((hostname = get_cookie("host")) != NULL)) {
				if (*hostname) {
					cmd = (char *)realloc(cmd, 1024 + strlen(hostname));
					sprintf(cmd + strlen(cmd), " host=^%s$", hostname);
					gotfilter = 1;
				}
			}

			if (obeycookies && !gotfilter && ((pagename = get_cookie("pagepath")) != NULL)) {
				if (*pagename) {
					cmd = (char *)realloc(cmd, 1024 + 2*strlen(pagename));
					sprintf(cmd + strlen(cmd), " page=^%s$|^%s/.+", pagename, pagename);
					gotfilter = 1;
				}
			}

			sres = newsendreturnbuf(1, NULL);

			if (sendmessage(cmd, NULL, XYMON_TIMEOUT, sres) == XYMONSEND_OK) {
				char *bol, *eoln;
				int first = 1;

				respbuf = getsendreturnstr(sres, 1);

				bol = respbuf;
				while (bol) {
					char *hname, *tname, *ackcode;

					eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
					hname = tname = ackcode = NULL;
					hname = strtok(bol, "|");
					if (hname) tname = strtok(NULL, "|");
					if (tname) ackcode = strtok(NULL, "|");
					if (hname && tname && ackcode && (strcmp(hname, "summary") != 0)) {
						if (first) {
							fprintf(stdout, "<form method=\"POST\" ACTION=\"%s\">\n", getenv("SCRIPT_NAME"));
							fprintf(stdout, "<center><table cellpadding=5 summary=\"Ack data\">\n");
							fprintf(stdout, "<tr><th align=left>Host</th><th align=left>Test</th><th align=left>Duration<br>(minutes)</th><th align=left>Cause</th><th>Ack</th><th>Ack Multiple</tr>\n");
							first = 0;
						}

						generate_ackline(stdout, hname, tname, ackcode);
					}

					if (eoln) bol = eoln+1; else bol = NULL;
				}

				if (first) {
					fprintf(stdout, "<center><font size=\"+1\"><b>No active alerts</b></font></center\n");
				}
				else {
					generate_ackline(stdout, NULL, NULL, NULL);
					fprintf(stdout, "</table></center>\n");
					fprintf(stdout, "</form>\n");
				}
			}

			freesendreturnbuf(sres);

			headfoot(stdout, "acknowledge", "", "footer", COL_RED);
		}
	}
	else if ( (nopin && (cgi_method == CGI_POST)) || (!nopin && (cgidata != NULL)) ) {
		char *xymonmsg;
		char *acking_user = "";
		acklist_t *awalk;
		char msgline[4096];
		strbuffer_t *response = newstrbuffer(0);
		int count = 0;

		parse_query();
		if (getenv("REMOTE_USER")) {
			acking_user = (char *)malloc(50 + strlen(getenv("REMOTE_USER")));
			sprintf(acking_user, "\nAcked by: %s", getenv("REMOTE_USER"));
			if (getenv("REMOTE_ADDR")) {
				char *p = acking_user + strlen(acking_user);
				sprintf(p, " (%s)", getenv("REMOTE_ADDR"));
			}
		}

		addtobuffer(response, "<center>\n");
		for (awalk = ackhead; (awalk); awalk = awalk->next) {
			if (!awalk->checked) continue;

			if ((reqtype == ACK_ONE) && (awalk->id != sendnum)) continue;

			if (reqtype == ACK_MANY) {
				if (!awalk->ackmsg) awalk->ackmsg = ackmsgall;
				if (!awalk->validity && validityall) awalk->validity = durationvalue(validityall);
			}

			count++;
			if (!awalk->ackmsg || !awalk->validity || !awalk->acknum) {
				if (awalk->hostname && awalk->testname) {
					sprintf(msgline, "<b>NO ACK</b> sent for host %s / test %s",
						awalk->hostname, awalk->testname);
				}
				else {
					sprintf(msgline, "<b>NO ACK</b> sent for item %d", awalk->id);
				}
				addtobuffer(response, msgline);
				addtobuffer(response, ": Duration or message not set<br>\n");
				continue;
			}

			xymonmsg = (char *)malloc(1024 + strlen(awalk->ackmsg) + strlen(acking_user));
			sprintf(xymonmsg, "xymondack %d %d %s %s", awalk->acknum, awalk->validity, awalk->ackmsg, acking_user);
			if (sendmessage(xymonmsg, NULL, XYMON_TIMEOUT, NULL) == XYMONSEND_OK) {
				if (awalk->hostname && awalk->testname) {
					sprintf(msgline, "Acknowledge sent for host %s / test %s<br>\n", 
						awalk->hostname, awalk->testname);
				}
				else {
					sprintf(msgline, "Acknowledge sent for code %d<br>\n", awalk->acknum);
				}
			}
			else {
				if (awalk->hostname && awalk->testname) {
					sprintf(msgline, "Failed to send acknowledge for host %s / test %s<br>\n", 
						awalk->hostname, awalk->testname);
				}
				else {
					sprintf(msgline, "Failed to send acknowledge for code %d<br>\n", awalk->acknum);
				}
			}

			addtobuffer(response, msgline);
			xfree(xymonmsg);
		}

		if (count == 0) addtobuffer(response, "<b>No acks requested</b>\n");

		addtobuffer(response, "</center>\n");

		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	
		headfoot(stdout, "acknowledge", "", "header", COL_RED);
		fprintf(stdout, "%s", STRBUF(response));
		headfoot(stdout, "acknowledge", "", "footer", COL_RED);
	}

	return 0;
}

