/*----------------------------------------------------------------------------*/
/* Hobbit alert acknowledgment CGI tool.                                      */
/*                                                                            */
/* This is a CGI script for handling acknowledgments of alerts.               */
/* If called with no CGI query, it will present the acknowledgment form;      */
/* if called with a proper CGI query string it will send an ack-message to    */
/* the Hobbit daemon.                                                         */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-ack.c,v 1.29 2006-07-12 07:01:03 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libbbgen.h"
#include "version.h"

static char *action = "";
static int  acknum = 0;
static int  validity = 0;
static char *ackmsg = "";
static cgidata_t *cgidata = NULL;
static int  nopin = 0;

static void parse_query(void)
{
	cgidata_t *cwalk;
	int sendnum = 0;
	char numberitm[30], delayitm[30], messageitm[30];

	for (cwalk=cgidata; (cwalk); cwalk = cwalk->next) {
		if (strncmp(cwalk->name, "Send_", 5) == 0) sendnum = atoi(cwalk->name+5);
	}

	if (sendnum) {
		sprintf(numberitm,  "NUMBER_%d",  sendnum);
		sprintf(delayitm,   "DELAY_%d",   sendnum);
		sprintf(messageitm, "MESSAGE_%d", sendnum);
	}
	else {
		*numberitm = *delayitm = *messageitm = '\0';
	}

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "ACTION") == 0) {
			action = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NUMBER") == 0) {
			acknum = atoi(cwalk->value);
		}
		else if (sendnum && (strcasecmp(cwalk->name, numberitm) == 0)) {
			acknum = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "DELAY") == 0) {
			validity = atoi(cwalk->value);
		}
		else if (sendnum && (strcasecmp(cwalk->name, delayitm) == 0)) {
			validity = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "MESSAGE") == 0) {
			ackmsg = strdup(cwalk->value);
		}
		else if (sendnum && (strcasecmp(cwalk->name, messageitm) == 0)) {
			ackmsg = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}

void generate_ackline(FILE *output, char *hname, char *tname, char *ackcode)
{
	static int num = 0;

	num++;
	fprintf(output, "<tr>\n");

	fprintf(output, "    <td>%s</td>\n", hname);

	fprintf(output, "    <td>%s</td>\n", tname);
	fprintf(output, "    <TD><INPUT TYPE=TEXT NAME=\"DELAY_%d\" VALUE=\"60\" SIZE=4 MAXLENGTH=4></TD>\n", num);
	fprintf(output, "    <TD><INPUT TYPE=TEXT NAME=\"MESSAGE_%d\" SIZE=60 MAXLENGTH=80></TD>\n", num);
	fprintf(output, "    <TD>\n");
	fprintf(output, "       <INPUT TYPE=\"HIDDEN\" NAME=\"NUMBER_%d\" SIZE=7 MAXLENGTH=7 VALUE=\"%s\">\n", num, ackcode);
	fprintf(output, "       <INPUT TYPE=\"SUBMIT\" NAME=\"Send_%d\" VALUE=\"Send\" ALT=\"Send\">\n", num);
	fprintf(output, "    </TD>\n");

	fprintf(output, "</tr>\n");
}

int main(int argc, char *argv[])
{
	int argi, bbresult;
	char *respmsgfmt = "";
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
		else if (strcmp(argv[argi], "--no-pin") == 0) {
			nopin = 1;
		}
	}

	redirect_cgilog("bb-ack");

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_RED), NULL);

		printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

		if (!nopin) {
			showform(stdout, "acknowledge", "acknowledge_form", COL_RED, getcurrenttime(NULL), 
				 NULL, NULL);
		}
		else {
			char cmd[1024];
			char *respbuf = NULL;
                	char *cookie = NULL, *p;
			int gotfilter = 0;

			headfoot(stdout, "acknowledge", "", "header", COL_RED);

			strcpy(cmd, "hobbitdboard color=red,yellow fields=hostname,testname,cookie");

			p = getenv("HTTP_COOKIE"); 
			if (p) cookie = strdup(p);

			if (cookie && ((p = strstr(cookie, "host=")) != NULL)) {
				char *hostname;
				
				hostname = p + strlen("host=");
				p = strchr(hostname, ';'); if (p) *p = '\0';
				if (*hostname) {
					sprintf(cmd + strlen(cmd), " host=%s", hostname);
					gotfilter = 1;
				}
				if (p) *p = ';';
			}

			if (cookie && !gotfilter && ((p = strstr(cookie, "pagepath=")) != NULL)) {
				char *pagename;

				pagename = p + strlen("pagepath=");
				p = strchr(pagename, ';'); if (p) *p = '\0';
				if (*pagename) {
					sprintf(cmd + strlen(cmd), " page=^%s$", pagename);
					gotfilter = 1;
				}
				if (p) *p = ';';
			}
			xfree(cookie);

			if (sendmessage(cmd, NULL, NULL, &respbuf, 1, BBTALK_TIMEOUT) == BB_OK) {
				char *bol, *eoln;
				int first = 1;

				bol = respbuf;
				while (bol) {
					char *hname, *tname, *ackcode;

					eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
					hname = tname = ackcode = NULL;
					hname = strtok(bol, "|");
					if (hname) tname = strtok(NULL, "|");
					if (tname) ackcode = strtok(NULL, "|");
					if (hname && tname && ackcode) {
						if (first) {
							fprintf(stdout, "<form method=\"POST\" ACTION=\"%s\">\n", getenv("SCRIPT_NAME"));
							fprintf(stdout, "<center><table cellpadding=5 summary=\"Ack data\">\n");
							fprintf(stdout, "<tr><th align=left>Host</th><th align=left>Test</th><th align=left>Duration<br>(minutes)</th><th align=left>Cause</th></tr>\n");
							first = 0;
						}

						generate_ackline(stdout, hname, tname, ackcode);
					}

					if (eoln) bol = eoln+1; else bol = NULL;
				}

				if (!first) {
					fprintf(stdout, "</table></center>\n");
					fprintf(stdout, "<INPUT TYPE=\"HIDDEN\" NAME=\"ACTION\" VALUE=\"Ack\">\n");
					fprintf(stdout, "</form>\n");
				}
			}

			headfoot(stdout, "acknowledge", "", "footer", COL_RED);
		}
		return 0;
	}

	parse_query();

	if (strcasecmp(action, "ack") == 0) {
		char *bbmsg;
		char *acking_user = "";

		if (getenv("REMOTE_USER")) {
			acking_user = (char *)malloc(50 + strlen(getenv("REMOTE_USER")));
			sprintf(acking_user, "\nAcked by: %s", getenv("REMOTE_USER"));
			if (getenv("REMOTE_ADDR")) {
				char *p = acking_user + strlen(acking_user);
				sprintf(p, " (%s)", getenv("REMOTE_ADDR"));
			}
		}

		bbmsg = (char *)malloc(1024 + strlen(ackmsg) + strlen(acking_user));
		sprintf(bbmsg, "hobbitdack %d %d %s %s", acknum, validity, ackmsg, acking_user);
		bbresult = sendmessage(bbmsg, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
		if (bbresult != BB_OK) {
			respmsgfmt = "<center><h4>Could not contact %s servers</h4></center>\n";
		}
		else {
			respmsgfmt = "<center><h4>Acknowledgment sent to %s servers</h4></center>\n";
		}

		if (strlen(acking_user)) xfree(acking_user);
		xfree(bbmsg);
	}
	else if (strcasecmp(action, "page") == 0) {
		respmsgfmt = "<center><h4>This system does not support paging the operator</h4></center>\n";
	}
	else {
		respmsgfmt = "<center><h4>Unknown action ignored</h4></center>\n";
	}

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	
	headfoot(stdout, "acknowledge", "", "header", COL_RED);
	fprintf(stdout, respmsgfmt, "Hobbit");
	headfoot(stdout, "acknowledge", "", "footer", COL_RED);

	return 0;
}

