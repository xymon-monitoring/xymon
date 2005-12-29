/*----------------------------------------------------------------------------*/
/* Hobbit CGI for sending in an "ackinfo" message.                            */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-ackinfo.c,v 1.1 2005-12-29 16:31:35 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#include "libbbgen.h"

static char *hostname = NULL;
static char *testname = NULL;
static int level = 0;
static int validity = 0;
static char *ackedby = NULL;
static char *ackmsg  = NULL;

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "HOST") == 0) {
			hostname = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "SERVICE") == 0) {
			testname = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NOTE") == 0) {
			ackmsg = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "LEVEL") == 0) {
			level = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "VALIDITY") == 0) {
			validity = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "ACKEDBY") == 0) {
			ackedby = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}


int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *bbmsg;
	int res;

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
		else if (argnmatch(argv[argi], "--level=")) {
			char *p = strchr(argv[argi], '=');
			level = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--validity=")) {
			char *p = strchr(argv[argi], '=');
			validity = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--sender=")) {
			char *p = strchr(argv[argi], '=');
			ackedby = strdup(p+1);
		}
	}

	redirect_cgilog("hobbit-ackinfo");
	parse_query();

	if (hostname && *hostname && testname && *testname && (validity>0) && ackedby && *ackedby && ackmsg && *ackmsg) {
		char *p;

		p = strchr(ackedby, '\n'); if (p) *p = '\0';
		p = strchr(ackmsg, '\n'); if (p) *p = '\0';

		/* ackinfo HOST.TEST\nlevel\nvaliduntil\nackedby\nmsg */
		bbmsg = (char *)malloc(1024 + strlen(hostname) + strlen(testname) + strlen(ackedby) + strlen(ackmsg));
		sprintf(bbmsg, "ackinfo %s.%s\n%d\n%d\n%s\n%s\n",
			hostname, testname, level, validity, ackedby, ackmsg);
		res = sendmessage(bbmsg, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
	}

	fprintf(stdout, "Content-type: text/html\n\n");

	return 0;
}

