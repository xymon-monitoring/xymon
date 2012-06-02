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

static char *queryfilter = NULL;
static char *boardcmd  = "xymondboard";
static char *fieldlist = "fields=hostname,testname,color,lastchange,logtime,cookie,acktime,ackmsg,disabletime,dismsg,line1";
static char *colorlist = "color=red,yellow,purple";

static void errormsg(char *msg)
{
	fprintf(stderr, 
		 "Content-type: %s\n\n<html><head><title>Invalid request</title></head>\n<body>%s</body></html>\n", 
		 xgetenv("HTMLCONTENTTYPE"), msg);
}

static int parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "filter") == 0) {
			queryfilter = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}

	if (!queryfilter) queryfilter = "";

	/* See if the query includes a color filter - this overrides our default */
	if ((strncmp(queryfilter, "color=", 6) == 0) || (strstr(queryfilter, " color=") != NULL)) colorlist = "";

	return 0;
}

char *extractline(char *ptn, char **src)
{
	char *pos = strstr(*src, ptn);
	char *eoln;

	if (pos == NULL) return NULL;

	eoln = strchr(pos, '\n');
	if (eoln) *eoln = '\0';

	if (pos == *src) 
		*src = eoln+1;
	else
		*(pos-1) = '\0';

	if (pos) pos += strlen(ptn);

	return pos;
}


int main(int argc, char **argv)
{
	int argi;
	char *criticalconfig = NULL;
	char *accessfn = NULL;
	char *userid = getenv("REMOTE_USER");

	FILE *output = stdout;

	char *xymondreq;
	sendreturn_t *sres;
	int xymondresult;
	char *log, *bol, *eoln, *endkey;

	libxymon_init(argv[0]);
	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--hobbit") == 0) {
			boardcmd = "hobbitdboard";
		}
		else if (argnmatch(argv[argi], "--critical=")) {
			char *p = strchr(argv[argi], '=');
			criticalconfig = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--access=")) {
			char *p = strchr(argv[argi], '=');
			accessfn = strdup(p+1);
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
	}

	/* Setup the query for xymond */
	parse_query();
	xymondreq = (char *)malloc(strlen(boardcmd) + strlen(fieldlist) + strlen(colorlist) + strlen(queryfilter) + 5);
	sprintf(xymondreq, "%s %s %s %s", boardcmd, fieldlist, colorlist, queryfilter);

	/* Get the current status */
	sres = newsendreturnbuf(1, NULL);
	xymondresult = sendmessage(xymondreq, NULL, XYMON_TIMEOUT, sres);
	if (xymondresult != XYMONSEND_OK) {
		char *errtxt = (char *)malloc(1024 + strlen(xymondreq));
		sprintf(errtxt, "Status not available: Req=%s, result=%d\n", htmlquoted(xymondreq), xymondresult);
		errormsg(errtxt);
		return 1;
	}
	else {
		log = getsendreturnstr(sres, 1);
	}
	freesendreturnbuf(sres);

	/* Load the host data (for access control) */
	if (accessfn) {
		load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
		load_web_access_config(accessfn);
	}

	/* Load the critical config */
	if (criticalconfig) load_critconfig(criticalconfig);

	fprintf(output, "Content-type: text/xml\n\n");
	fprintf(output, "<?xml version='1.0' encoding='ISO-8859-1'?>\n");
	fprintf(output, "<StatusBoard>\n");

	/* Step through the status board, one line at a time */
	bol = log;
	while (bol && *bol) {
		int useit = 1;
		char *hostname, *testname, *color, *txt, *lastchange, *logtime, *cookie, *acktime, *ackmsg, *distime, *dismsg;

		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';

		if (criticalconfig) {
			critconf_t *cfg;

			/* The key for looking up items in the critical config is "hostname|testname", which we already have */
			endkey = strchr(bol, '|'); if (endkey) endkey = strchr(endkey+1, '|');
			*endkey = '\0';
			cfg = get_critconfig(bol, CRITCONF_TIMEFILTER, NULL);
			*endkey = '|';

			if (!cfg) useit = 0;
		}

		if (useit) {
			hostname = gettok(bol, "|");
			testname = (hostname ? gettok(NULL, "|") : NULL);

			if (accessfn) useit = web_access_allowed(userid, hostname, testname, WEB_ACCESS_VIEW);
		}

		if (useit) {
			color = (testname ? gettok(NULL, "|") : NULL);
			lastchange = (color ? gettok(NULL, "|") : NULL);
			logtime = (lastchange ? gettok(NULL, "|") : NULL);
			cookie = (logtime ? gettok(NULL, "|") : NULL);
			acktime = (cookie ? gettok(NULL, "|") : NULL);
			ackmsg = (acktime ? gettok(NULL, "|") : NULL);
			distime = (ackmsg ? gettok(NULL, "|") : NULL);
			dismsg = (distime ? gettok(NULL, "|") : NULL);
			txt = (dismsg ? gettok(NULL, "|") : NULL);

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
					char *ackedby;

					nldecode(ackmsg);
					ackedby = extractline("Acked by: ", &ackmsg);

					fprintf(output, "  <AckTime>%s</AckTime>\n", acktime);
					fprintf(output, "  <AckText><![CDATA[%s]]></AckText>\n", ackmsg);
					if (ackedby) fprintf(output, "  <AckedBy><![CDATA[%s]]></AckedBy>\n", ackedby);
				}
				if (atoi(distime) != 0) {
					char *disabledby;

					nldecode(dismsg);
					disabledby = extractline("Disabled by: ", &dismsg);
					if (strncmp(dismsg, "Reason: ", 8) == 0) dismsg += 8;

					fprintf(output, "  <DisableTime>%s</DisableTime>\n", distime);
					fprintf(output, "  <DisableText><![CDATA[%s]]></DisableText>\n", dismsg);
					if (disabledby) fprintf(output, "  <DisabledBy><![CDATA[%s]]></DisabledBy>\n", disabledby);
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

