/*----------------------------------------------------------------------------*/
/* Hobbit status-log viewer CGI.                                              */
/*                                                                            */
/* This CGI tool shows an HTML version of a status log.                       */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc.c,v 1.38 2005-05-07 07:00:56 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libbbgen.h"
#include "version.h"
#include "hobbitsvc-info.h"
#include "hobbitsvc-trends.h"

/* Commandline params */
static enum { SRC_HOBBITD, SRC_HISTLOGS, SRC_MEM } source = SRC_HOBBITD;
static int wantserviceid = 1;
static char *multigraphs = ",disk,inode,qtree,";

/* CGI params */
static char *hostname = NULL;
static char *service = NULL;
static char *ip = NULL;
static char *displayname = NULL;
static char *tstamp = NULL;

static char errortxt[1000];

static void errormsg(char *msg)
{
	snprintf(errortxt, sizeof(errortxt),
		 "Content-type: text/html\n\n<html><head><title>Invalid request</title></head>\n<body>%s</body></html>\n", 
		 msg);

	errortxt[sizeof(errortxt)-1] = '\0';
}

static int parse_query(void)
{
	char *query, *token;

	if (xgetenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request");
		return 1;
	}
	else query = urldecode("QUERY_STRING");

	token = strtok(query, "&");
	while (token) {
		char *val;
		int n = 0;

		val = strchr(token, '='); 
		if (val) { 
			*val = '\0'; 
			val++;
			n = strcspn(val, "/");
		}

		if (val && argnmatch(token, "HOSTSVC")) {
			if (n == strlen(val)) {
				char *p = strrchr(val, '.');
				if (p) { *p = '\0'; service = strdup(p+1); }
				hostname = strdup(val);
				while ((p = strchr(hostname, ','))) *p = '.';
			}
		}
		else if (val && argnmatch(token, "IP")) {
			ip = strdup(val);
		}
		else if (val && argnmatch(token, "DISPLAYNAME")) {
			displayname = strdup(val);
		}
		else if (val && argnmatch(token, "HOST")) {
			if (n == strlen(val)) hostname = strdup(val);
		}
		else if (val && argnmatch(token, "SERVICE")) {
			if (n == strlen(val)) service = strdup(val);
		}
		else if (val && argnmatch(token, "TIMEBUF")) {
			if (n == strlen(val)) tstamp = strdup(val);
		}

		token = strtok(NULL, "&");
	}

        xfree(query);

	if (!hostname || !service) {
		errormsg("Invalid request");
		return 1;
	}

	return 0;
}

int do_request(void)
{
	static time_t lastload = 0;
	time_t now = time(NULL);
	int color;
	char timesincechange[100];
	time_t logtime = 0, acktime = 0, disabletime = 0;
	char *log = NULL, *firstline = NULL, *sender = NULL, *flags = NULL;	/* These are free'd */
	char *restofmsg = NULL, *ackmsg = NULL, *dismsg = NULL;			/* These are just used */

	if (parse_query() != 0) return 1;

	if ((lastload + 300) < now) {
		load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
		lastload = now;
	}

	if (!hostinfo(hostname)) {
		errormsg("No such host");
		return 1;
	}

	if ((strcmp(service, xgetenv("LARRDCOLUMN")) == 0) || (strcmp(service, xgetenv("INFOCOLUMN")) == 0)) {
		sethostenv_refresh(600);
		color = COL_GREEN;
		logtime = time(NULL);
		strcpy(timesincechange, "0 minutes");

		if (strcmp(service, xgetenv("LARRDCOLUMN")) == 0) {
			log = restofmsg = generate_trends(hostname);
		}
		else if (strcmp(service, xgetenv("INFOCOLUMN")) == 0) {
			log = restofmsg = generate_info(hostname);
		}
	}
	else if (source == SRC_HOBBITD) {
		char hobbitdreq[200];
		int hobbitdresult;
		char *items[20];
		int icount;
		time_t logage;
		char *sumline, *msg, *p;

		sethostenv_refresh(60);
		sprintf(hobbitdreq, "hobbitdlog %s.%s", hostname, service);
		hobbitdresult = sendmessage(hobbitdreq, NULL, NULL, &log, 1, 30);
		if ((hobbitdresult != BB_OK) || (log == NULL) || (strlen(log) == 0)) {
			errormsg("Status not available\n");
			return 1;
		}

		sumline = log; p = strchr(log, '\n'); *p = '\0';
		msg = (p+1); p = strchr(msg, '\n');
		if (!p) {
			firstline = strdup(msg);
			restofmsg = NULL;
		}
		else { 
			*p = '\0'; 
			firstline = strdup(msg); 
			restofmsg = (p+1);
			*p = '\n'; 
		}

		memset(items, 0, sizeof(items));
		p = gettok(sumline, "|"); icount = 0;
		while (p && (icount < 20)) {
			items[icount++] = p;
			p = gettok(NULL, "|");
		}

		/* hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|ackmsg|dismsg */
		color = parse_color(items[2]);
		flags = strdup(items[3]);
		logage = time(NULL) - atoi(items[4]);
		timesincechange[0] = '\0'; p = timesincechange;
		if (logage > 86400) p += sprintf(p, "%d days,", (int) (logage / 86400));
		p += sprintf(p, "%d hours, %d minutes", (int) ((logage % 86400) / 3600), (int) ((logage % 3600) / 60));
		logtime = atoi(items[5]);
		if (items[7] && strlen(items[7])) acktime = atoi(items[7]);
		if (items[8] && strlen(items[8])) disabletime = atoi(items[8]);
		sender = strdup(items[9]);

		if (items[11] && strlen(items[11])) ackmsg = items[11];
		if (ackmsg) nldecode(ackmsg);

		if (items[12] && strlen(items[12])) dismsg = items[12];
		if (dismsg) nldecode(dismsg);
	}
	else if (source == SRC_HISTLOGS) {
		char logfn[PATH_MAX];
		struct stat st;
		int fd;
		/*
		 * Some clients (Unix disk reports) dont have a newline before the
		 * "Status unchanged in ..." text. Most do, but at least Solaris and
		 * AIX do not. So just look for the text, not the newline.
		 */
		char *statusunchangedtext = "Status unchanged in ";
		char *receivedfromtext = "Message received from ";
		char *p, *unchangedstr, *receivedfromstr, *hostnamedash;
		int n;

		if (!tstamp) errormsg("Invalid request");

		hostnamedash = strdup(hostname);
		p = hostnamedash; while ((p = strchr(p, '.')) != NULL) *p = '_';
		p = hostnamedash; while ((p = strchr(p, ',')) != NULL) *p = '_';
		sprintf(logfn, "%s/%s/%s/%s", xgetenv("BBHISTLOGS"), hostnamedash, service, tstamp);
		xfree(hostnamedash);
		p = tstamp; while ((p = strchr(p, '_')) != NULL) *p = ' ';
		sethostenv_histlog(tstamp);

		if (stat(logfn, &st) == -1) {
			errormsg("No such host/service\n");
			return 1;
		}

		fd = open(logfn, O_RDONLY);
		if (fd < 0) {
			errormsg("Unable to access logfile\n");
			return 1;
		}
		log = (char *)malloc(st.st_size+1);
		read(fd, log, st.st_size);
		close(fd);

		p = strchr(log, '\n'); 
		if (!p) {
			firstline = strdup(log);
			restofmsg = NULL;
		}
		else { 
			*p = '\0'; 
			firstline = strdup(log); 
			restofmsg = (p+1);
			*p = '\n'; 
		}


		color = parse_color(log);

		p = strstr(log, "<!-- [flags:"); 
		if (p) {
			p += strlen("<!-- [flags:");
			n = strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
			flags = (char *)malloc(n+1);
			strncpy(flags, p, n);
			*(flags + n) = '\0';
		}

		timesincechange[0] = '\0';
		p = unchangedstr = strstr(restofmsg, statusunchangedtext);
		if (p) {
			p += strlen(statusunchangedtext);
			n = strcspn(p, "\n"); if (n >= sizeof(timesincechange)) n = sizeof(timesincechange);
			strncpy(timesincechange, p, n);
			timesincechange[n] = '\0';
		}

		p = receivedfromstr = strstr(restofmsg, receivedfromtext); 
		if (p) {
			p += strlen(receivedfromtext);
			n = strspn(p, "0123456789.");
			sender = (char *)malloc(n+1);
			strncpy(sender, p, n);
			*(sender+n) = '\0';
		}

		/* Kill the "Status unchanged ..." and "Message received ..." lines */
		if (unchangedstr) *unchangedstr = '\0';
		if (receivedfromstr) *receivedfromstr = '\0';
	}

	fprintf(stdout, "Content-type: text/html\n\n");

	generate_html_log(hostname, 
			  (displayname ? displayname : hostname), 
			  service, 
			  (ip ? ip : ""), 
		          color, 
			  (sender ? sender : "Hobbit"), 
			  (flags ? flags : ""),
		          logtime, timesincechange, 
		          (firstline ? firstline : ""), 
			  (restofmsg ? restofmsg : ""), 
			  acktime, ackmsg, 
			  disabletime, dismsg,
		          (source == SRC_HISTLOGS), 
			  wantserviceid, 
			  (strcmp(service, xgetenv("INFOCOLUMN")) == 0),
			  (source == SRC_HOBBITD),
			  multigraphs,
			  stdout);

	/* Cleanup CGI params */
	if (hostname) xfree(hostname);
	if (displayname) xfree(displayname);
	if (service) xfree(service);
	if (ip) xfree(ip);
	if (tstamp) xfree(tstamp);

	/* Cleanup main vars */
	if (sender) xfree(sender);
	if (flags) xfree(flags);
	if (firstline) xfree(firstline);
	if (log) xfree(log);

	return 0;
}


int main(int argc, char *argv[])
{
	int argi;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--historical") == 0) {
			source = SRC_HISTLOGS;
		}
		else if (strcmp(argv[argi], "--hobbitd") == 0) {
			source = SRC_HOBBITD;
		}
		else if (strncmp(argv[argi], "--history=", 10) == 0) {
			char *val = strchr(argv[argi], '=')+1;

			if (strcmp(val, "none") == 0)
				histlocation = HIST_NONE;
			else if (strcmp(val, "top") == 0)
				histlocation = HIST_TOP;
			else if (strcmp(val, "bottom") == 0)
				histlocation = HIST_BOTTOM;
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, NULL);
		}
		else if (strcmp(argv[argi], "--no-svcid") == 0) {
			wantserviceid = 0;
		}
		else if (argnmatch(argv[argi], "--templates=")) {
			char *p = strchr(argv[argi], '=');
			sethostenv_template(p+1);
		}
		else if (argnmatch(argv[argi], "--multigraphs=")) {
			char *p = strchr(argv[argi], '=');
			multigraphs = (char *)malloc(strlen(p+1) + 3);
			sprintf(multigraphs, ",%s,", p+1);
		}
		else if (strcmp(argv[argi], "--no-disable") == 0) {
			showenadis = 0;
		}
		else if (strcmp(argv[argi], "--no-jsvalidation") == 0) {
			usejsvalidation = 0;
		}
	}

	*errortxt = '\0';
	hostname = displayname = service = ip = tstamp = NULL;
	if (do_request() != 0) {
		fprintf(stdout, "%s", errortxt);
		return 1;
	}

	return 0;
}

