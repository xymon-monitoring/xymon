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

static char rcsid[] = "$Id: bb-ack.c,v 1.19 2005-06-06 20:06:56 henrik Exp $";

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

/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 *      ACTION=action&NUMBER=acknum&DELAY=validity&MESSAGE=text
 */

char *reqenv[] = {
	"BBDISP",
	"BBHOME",
	NULL 
};

static char *action = "";
static int  acknum = 0;
static int  validity = 0;
static char *ackmsg = "";

static void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(void)
{
	char *query, *token;

	if (xgetenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request");
		return;
	}
	else query = urldecode("QUERY_STRING");

	token = strtok(query, "&");
	while (token) {
		char *val;
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }
		if (argnmatch(token, "ACTION")) {
			action = strdup(val);
		}
		else if (argnmatch(token, "NUMBER")) {
			acknum = atoi(val);
		}
		else if (argnmatch(token, "DELAY")) {
			validity = atoi(val);
		}
		else if (argnmatch(token, "MESSAGE")) {
			ackmsg = strdup(val);
		}

		token = strtok(NULL, "&");
	}

        xfree(query);
}

int main(int argc, char *argv[])
{
	int argi, bbresult;
	char bbmsg[MAXMSG];
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

	if ((xgetenv("QUERY_STRING") == NULL) || (strlen(xgetenv("QUERY_STRING")) == 0)) {
		/* Present the query form */
		int formfile;
		char formfn[PATH_MAX];

		sprintf(formfn, "%s/web/acknowledge_form", xgetenv("BBHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1);
			read(formfile, inbuf, st.st_size);
			inbuf[st.st_size] = '\0';
			close(formfile);

			printf("Content-Type: text/html\n\n");
			sethostenv("", "", "", colorname(COL_RED), NULL);

			headfoot(stdout, "acknowledge", "", "header", COL_RED);
			output_parsed(stdout, inbuf, COL_RED, "acknowledge", time(NULL));
			headfoot(stdout, "acknowledge", "", "footer", COL_RED);

			xfree(inbuf);
		}
		return 0;
	}

	envcheck(reqenv);
	parse_query();

	if (strcasecmp(action, "ack") == 0) {
		char *acking_user = "";

		if (getenv("REMOTE_USER")) {
			acking_user = (char *)malloc(50 + strlen(getenv("REMOTE_USER")));
			sprintf(acking_user, "\nAcked by: %s", getenv("REMOTE_USER"));
			if (getenv("REMOTE_ADDR")) {
				char *p = acking_user + strlen(acking_user);
				sprintf(p, " (%s)", getenv("REMOTE_ADDR"));
			}
		}

		sprintf(bbmsg, "hobbitdack %d %d %s %s", acknum, validity, ackmsg, acking_user);
		bbresult = sendmessage(bbmsg, NULL, NULL, NULL, 0, 30);
		if (bbresult != BB_OK) {
			respmsgfmt = "<center><h4>Could not contact %s servers</h4></center>\n";
		}
		else {
			respmsgfmt = "<center><h4>Acknowledgment sent to %s servers</h4></center>\n";
		}

		if (strlen(acking_user)) xfree(acking_user);
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

