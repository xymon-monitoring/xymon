/*----------------------------------------------------------------------------*/
/* Xymon status-log viewer CGI.                                               */
/*                                                                            */
/* This CGI provides an XML interface to the xymondboard status. Intended for */
/* use by external user interfaces, e.g. smartphones.                         */
/*                                                                            */
/* Copyright (C) 2011 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: svcstatus.c 6765 2011-10-13 11:55:08Z storner $";

#include <limits.h>

#include <stdio.h>
#include <string.h>

#include "libxymon.h"

int main(int argc, char **argv)
{
	int argi;
	char *criticalconfig = NULL;
	char *envarea = NULL;

	char *xymondreq = "xymondboard color=red,yellow,purple fields=hostname,testname,color,lastchange,logtime,cookie,acktime,ackmsg,line1";
	FILE *output = stdout;

	sendreturn_t *sres;
	int xymondresult;
	char *log, *bol, *eoln, *endkey;

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
		else if (argnmatch(argv[argi], "--critical=")) {
			char *p = strchr(argv[argi], '=');
			criticalconfig = strdup(p+1);
		}
	}

	sres = newsendreturnbuf(1, NULL);
	xymondresult = sendmessage(xymondreq, NULL, XYMON_TIMEOUT, sres);
	if (xymondresult != XYMONSEND_OK) {
		char *errtxt = (char *)malloc(1024 + strlen(xymondreq));
		sprintf(errtxt, "Status not available: Req=%s, result=%d\n", htmlquoted(xymondreq), xymondresult);
		// errormsg(errtxt);
		return 1;
	}
	else {
		log = getsendreturnstr(sres, 1);
	}
	freesendreturnbuf(sres);

	/* Load the critical config */
	if (criticalconfig) load_critconfig(criticalconfig);

	fprintf(output, "Content-type: text/xml\n\n");
	fprintf(output, "<?xml version='1.0' encoding='ISO-8859-1'?>\n");
	fprintf(output, "<StatusBoard>\n");

	bol = log;
	while (bol && *bol) {
		int useit = 1;
		char *hostname, *testname, *color, *txt, *lastchange, *logtime, *cookie, *acktime, *ackmsg;

		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';

		if (criticalconfig) {
			critconf_t *cfg;

			endkey = strchr(bol, '|'); if (endkey) endkey = strchr(endkey+1, '|');
			*endkey = '\0';
			cfg = get_critconfig(bol, CRITCONF_TIMEFILTER, NULL);
			*endkey = '|';

			if (!cfg) useit = 0;
		}

		if (useit) {
			hostname = gettok(bol, "|");
			testname = (hostname ? gettok(NULL, "|") : NULL);
			color = (testname ? gettok(NULL, "|") : NULL);
			lastchange = (color ? gettok(NULL, "|") : NULL);
			logtime = (lastchange ? gettok(NULL, "|") : NULL);
			cookie = (logtime ? gettok(NULL, "|") : NULL);
			acktime = (cookie ? gettok(NULL, "|") : NULL);
			ackmsg = (acktime ? gettok(NULL, "|") : NULL);
			txt = (ackmsg ? gettok(NULL, "|") : NULL);

			if (txt) {
				/* We have all data */
				fprintf(output, "<ServerStatus>\n");
				fprintf(output, "  <Servername>%s</Servername>\n", hostname);
				fprintf(output, "  <Type>%s</Type>\n", testname);
				fprintf(output, "  <Status>%s</Status>\n", color);
				fprintf(output, "  <LastChange>%s</LastChange>\n", lastchange);
				fprintf(output, "  <LogTime>%s</LogTime>\n", logtime);
				fprintf(output, "  <Cookie>%s</Cookie>\n", cookie);
				if (atoi(acktime) != 0) {
					fprintf(output, "  <AckTime>%s</AckTime>\n", acktime);
					fprintf(output, "  <AckText><![CDATA[%s]]></AckText>\n", ackmsg);
				}
				fprintf(output, "  <MessageSummary><![CDATA[%s]]></MessageSummary>\n", txt);
				fprintf(output, "  <DetailURL><![CDATA[%s]]></DetailURL>\n", hostsvcurl(hostname, testname, 0));
				fprintf(output, "</ServerStatus>\n");
			}
		}

		if (eoln) {
			*eoln = '\n';
			bol = eoln+1;
		}
		else
			bol = NULL;
	}

	fprintf(output, "</StatusBoard>\n");
	xfree(log);

	return 0;
}

