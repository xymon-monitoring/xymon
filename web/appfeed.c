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
static char *queryhost = NULL;	/* host= and test=: one status, with its full text */
static char *querytest = NULL;

static void errormsg(char *msg)
{
	fprintf(stderr, 
		 "Refresh: 30\nContent-type: %s\n\n<html><head><title>Invalid request</title></head>\n<body>%s</body></html>\n", 
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
		else if (strcasecmp(cwalk->name, "host") == 0) {
			queryhost = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "test") == 0) {
			querytest = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}

	if (!queryfilter) queryfilter = "";

	/* See if the query includes a color filter - this overrides our default */
	if ((strncmp(queryfilter, "color=", 6) == 0) || (strstr(queryfilter, " color=") != NULL)) colorlist = "";

	return 0;
}

/*
 * Print text as XML character data, in the feed's charset: xmlquoted() in lib/
 * does the work (CDATA with "]]>" split, or & < > as entities, control bytes
 * dropped, and under UTF-8 each byte that is not well-formed UTF-8 shown as
 * U+FFFD so one stray byte cannot break the feed). Shared with perfdata and
 * xymond's XML replies.
 */
static void print_xml(FILE *output, char *s, int cdata)
{
	fputs(xmlquoted(s, htmlcontentutf8(), cdata), output);
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
	char *envarea = NULL;
	char *accessfn = NULL;
	char *userid = getenv("REMOTE_USER");

	FILE *output = stdout;

	SBUF_DEFINE(xymondreq);
	sendreturn_t *sres;
	int xymondresult;
	char *log, *bol, *eoln, *endkey;
	int detail = 0;

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
		else if (strcmp(argv[argi], "--hobbit") == 0) {
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
	}

	/* Setup the query for xymond */
	parse_query();
	if (queryhost || querytest) {
		/*
		 * One status, by its exact host and test name, whatever its colour,
		 * with the full message text added as the last field. The names are
		 * matched as written: a "." is a dot, not any character.
		 */
		char *hostre = (queryhost ? cgi_hostregex(queryhost) : NULL);
		char *testre = (querytest ? cgi_hostregex(querytest) : NULL);
		SBUF_DEFINE(detailfilter);

		if (!hostre || !testre) {
			errormsg("Invalid host or test name");
			return 1;
		}
		SBUF_MALLOC(detailfilter, strlen(hostre) + strlen(testre) + strlen(queryfilter) + 32);
		snprintf(detailfilter, detailfilter_buflen, "host=^%s$ test=^%s$ %s", hostre, testre, queryfilter);
		xfree(hostre); xfree(testre);
		queryfilter = detailfilter;
		colorlist = "";
		detail = 1;
	}
	SBUF_MALLOC(xymondreq, strlen(boardcmd) + strlen(fieldlist) + strlen(colorlist) + strlen(queryfilter) + 10);
	snprintf(xymondreq, xymondreq_buflen, "%s %s%s %s %s", boardcmd, fieldlist, (detail ? ",msg" : ""), colorlist, queryfilter);

	/* Get the current status */
	sres = newsendreturnbuf(1, NULL);
	xymondresult = sendmessage(xymondreq, NULL, XYMON_TIMEOUT, sres);
	if (xymondresult != XYMONSEND_OK) {
		SBUF_DEFINE(errtxt);

		SBUF_MALLOC(errtxt, 1024 + MAX_HTMLQUOTE_FACTOR*strlen(xymondreq));
		snprintf(errtxt, errtxt_buflen, "Status not available: Req=%s, result=%d\n", htmlquoted(xymondreq), xymondresult);
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

	fprintf(output, "Content-type: text/xml; charset=%s\n\n", htmlcontentcharset());
	fprintf(output, "<?xml version='1.0' encoding='%s'?>\n", htmlcontentcharset());
	fprintf(output, "<StatusBoard>\n");

	/* Step through the status board, one line at a time */
	bol = log;
	while (bol && *bol) {
		int useit = 1;
		char *hostname, *testname, *color, *txt, *lastchange, *logtime, *cookie, *acktime, *ackmsg, *distime, *dismsg, *msg = NULL;

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
			if (detail && txt) msg = gettok(NULL, "|");

			if (txt) {
				/* We have all data */
				fprintf(output, "<ServerStatus>\n");
				/* Under --ghosts=allow, the sending machine names the host */
				fprintf(output, "  <Servername>");
				print_xml(output, hostname, 0);
				fprintf(output, "</Servername>\n");
				fprintf(output, "  <Type>");
				print_xml(output, testname, 0);
				fprintf(output, "</Type>\n");
				fprintf(output, "  <Status>%s</Status>\n", color);
				fprintf(output, "  <LastChange>%s</LastChange>\n", lastchange);
				fprintf(output, "  <LogTime>%s</LogTime>\n", logtime);
				fprintf(output, "  <Cookie>%s</Cookie>\n", cookie);
				if (atoi(acktime) != 0) {
					char *ackedby;

					nldecode(ackmsg);
					ackedby = extractline("Acked by: ", &ackmsg);

					fprintf(output, "  <AckTime>%s</AckTime>\n", acktime);
					fprintf(output, "  <AckText>");
					print_xml(output, ackmsg, 1);
					fprintf(output, "</AckText>\n");
					if (ackedby) {
						fprintf(output, "  <AckedBy>");
						print_xml(output, ackedby, 1);
						fprintf(output, "</AckedBy>\n");
					}
				}
				if (atoi(distime) != 0) {
					char *disabledby;

					nldecode(dismsg);
					disabledby = extractline("Disabled by: ", &dismsg);
					if (strncmp(dismsg, "Reason: ", 8) == 0) dismsg += 8;

					fprintf(output, "  <DisableTime>%s</DisableTime>\n", distime);
					fprintf(output, "  <DisableText>");
					print_xml(output, dismsg, 1);
					fprintf(output, "</DisableText>\n");
					if (disabledby) {
						fprintf(output, "  <DisabledBy>");
						print_xml(output, disabledby, 1);
						fprintf(output, "</DisabledBy>\n");
					}
				}
				/* The first line is written by the monitored host: never raw */
				fprintf(output, "  <MessageSummary>");
				print_xml(output, txt, 1);
				fprintf(output, "</MessageSummary>\n");
				if (msg) {
					nldecode(msg);
					fprintf(output, "  <Message>");
					print_xml(output, msg, 1);
					fprintf(output, "</Message>\n");
				}
				fprintf(output, "  <DetailURL>");
				print_xml(output, hostsvcurl(hostname, testname, 0), 1);
				fprintf(output, "</DetailURL>\n");
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

