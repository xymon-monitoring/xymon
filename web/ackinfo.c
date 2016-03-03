/*----------------------------------------------------------------------------*/
/* Xymon CGI for sending in an "ackinfo" message.                             */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@storner.dk>                 */
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

static char *hostname = NULL;
static char *testname = NULL;
static int level = -1;
static int validity = -1;
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
		else if (strcasecmp(cwalk->name, "ALLTESTS") == 0) {
			if (strcasecmp(cwalk->value, "on") == 0) testname = strdup("*");
		}
		else if (strcasecmp(cwalk->name, "NOTE") == 0) {
			ackmsg = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "LEVEL") == 0) {
			/* Command line may override this */
			if (level == -1) level = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "VALIDITY") == 0) {
			/* Command line may override this */
			if (validity == -1) validity = atoi(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}


int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *xymonmsg;

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

	redirect_cgilog("ackinfo");

	/* We only want to accept posts from certain pages */
	{
	    char cgisource[1024]; char *p;
	    p = csp_header("ackinfo"); if (p) fprintf(stdout, "%s", p);
	    snprintf(cgisource, sizeof(cgisource), "%s/%s", xgetenv("CGIBINURL"), "svcstatus");
	    if (!cgi_refererok(cgisource)) {
		snprintf(cgisource, sizeof(cgisource), "%s/%s", xgetenv("CGIBINURL"), "criticalview");
		if (!cgi_refererok(cgisource)) {
			errprintf("ackinfo POST that is not coming from criticalview or svcstatus (referer=%s). Ignoring.\n", textornull(getenv("HTTP_REFERER")) );
			fprintf(stdout, "Location: %s.sh?\n\n", cgisource);
			return 0;
		}
	    }
	}

	parse_query();

	if (hostname && *hostname && testname && *testname && ((level == 0) || (validity>0)) && ackmsg && *ackmsg) {
		char *p;

		/* Get the login username */
		if (!ackedby) ackedby = getenv("REMOTE_USER");
		if (!ackedby) ackedby = "UnknownUser";

		if (validity == -1) validity = 30; /* 30 minutes */
		validity = validity*60;

		p = strchr(ackmsg, '\n'); if (p) *p = '\0';

		/* ackinfo HOST.TEST\nlevel\nvaliduntil\nackedby\nmsg */
		xymonmsg = (char *)malloc(1024 + strlen(hostname) + strlen(testname) + strlen(ackedby) + strlen(ackmsg));
		sprintf(xymonmsg, "ackinfo %s.%s\n%d\n%d\n%s\n%s\n",
			hostname, testname, level, validity, ackedby, ackmsg);
		sendmessage(xymonmsg, NULL, XYMON_TIMEOUT, NULL);
	}
	else {
		xymonmsg = (char *)malloc(1024 + (hostname ? strlen(hostname) : 9) + (testname ? strlen(testname) : 9) + (ackmsg ? strlen(ackmsg) : 9));
		sprintf(xymonmsg, "error in input params: hostname=%s, testname=%s, ackmsg=%s, validity=%d\n",
			(hostname ? hostname : "<unknown>"), (testname ? testname : "<unknown>"), (ackmsg ? ackmsg : "<unknown>"), validity);
	}

	fprintf(stdout, "Content-type: %s\n", xgetenv("HTMLCONTENTTYPE"));
	fprintf(stdout, "Location: %s\n", getenv("HTTP_REFERER"));
	fprintf(stdout, "\n");
	fprintf(stdout, "Sent to xymond:\n%s\n", htmlquoted(xymonmsg));

	return 0;
}

