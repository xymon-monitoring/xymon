/*----------------------------------------------------------------------------*/
/* Hobbit alert acknowledgment CGI tool.                                      */
/*                                                                            */
/* This is a CGI script for handling acknowledgments of alerts.               */
/* If called with no CGI query, it will present the acknowledgment form;      */
/* if called with a proper CGI query string it will send an ack-message to    */
/* the Hobbit daemon.                                                         */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-ack.c,v 1.22 2006-03-29 21:41:32 henrik Exp $";

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


static void parse_query(void)
{
	cgidata_t *cwalk;

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
		else if (strcasecmp(cwalk->name, "DELAY") == 0) {
			validity = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "MESSAGE") == 0) {
			ackmsg = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}
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
	}

	redirect_cgilog("bb-ack");

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_RED), NULL);
		showform(stdout, "acknowledge", "acknowledge_form", COL_RED, getcurrenttime(NULL), NULL);
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
		bbresult = sendmessage(bbmsg, NULL, NULL, NULL, 0, 30);
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

	fprintf(stdout, "Content-type: text/html\n\n");
	
	headfoot(stdout, "acknowledge", "", "header", COL_RED);
	fprintf(stdout, respmsgfmt, "Hobbit");
	headfoot(stdout, "acknowledge", "", "footer", COL_RED);

	return 0;
}

