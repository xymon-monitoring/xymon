/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "bb-hostsvc.sh" script                       */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc.c,v 1.20 2004-11-17 22:13:18 henrik Exp $";

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

#ifdef CGI
/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 *      HOSTSVC=www,sample,com.conn
 */

char *reqenv[] = {
	"BBDISP",
	"BBHOME",
	NULL 
};

static char *hostname = "";
static char *service = "";
static char *ip = "";
static char *displayname = "";
static char *tstamp = "";

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

	if (getenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request");
		return;
	}
	else query = urldecode("QUERY_STRING");

	if (!urlvalidate(query, NULL)) {
		errormsg("Invalid request");
		return;
	}

	token = strtok(query, "&");
	while (token) {
		char *val;
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }
		if (argnmatch(token, "HOSTSVC")) {
			char *p = strrchr(val, '.');

			if (p) { *p = '\0'; service = strdup(p+1); }
			hostname = strdup(val);
			while ((p = strchr(hostname, ','))) *p = '.';
		}
		else if (argnmatch(token, "IP")) {
			ip = strdup(val);
		}
		else if (argnmatch(token, "DISPLAYNAME")) {
			displayname = strdup(val);
		}
		else if (argnmatch(token, "HOST")) {
			hostname = strdup(val);
		}
		else if (argnmatch(token, "SERVICE")) {
			service = strdup(val);
		}
		else if (argnmatch(token, "TIMEBUF")) {
			tstamp = strdup(val);
		}

		token = strtok(NULL, "&");
	}

        free(query);

	if (strcmp(displayname, "") == 0) displayname = hostname;
}

int main(int argc, char *argv[])
{
	char bbgendreq[200];
	char *log = NULL;
	int bbgendresult;
	char *msg;
	char *sumline, *firstline, *restofmsg, *p;
	char *items[20];
	int argi, icount;
	int color;
	char timesincechange[100];
	time_t logtime = 0;
	char *sender;
	char *flags;
	char *ackmsg = NULL, *dismsg = NULL;
	enum source_t source = SRC_BBLOGS;

	getenv_default("USEBBGEND", "FALSE", NULL);
	if (strcmp(getenv("USEBBGEND"), "TRUE") == 0) source = SRC_BBGEND;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--historical") == 0) {
			source = SRC_HISTLOGS;
		}
		else if (strcmp(argv[argi], "--bbgend") == 0) {
			source = SRC_BBGEND;
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
			loadenv(p+1);
		}
	}

	envcheck(reqenv);
	parse_query();

	if (source == SRC_BBGEND) {
		time_t logage;

		sprintf(bbgendreq, "bbgendlog %s.%s", hostname, service);
		bbgendresult = sendmessage(bbgendreq, NULL, NULL, &log, 1, 30);
		if ((bbgendresult != BB_OK) || (log == NULL) || (strlen(log) == 0)) {
			errormsg("Status not available\n");
			return 1;
		}

		sumline = log; p = strchr(log, '\n'); *p = '\0';
		msg = (p+1); p = strchr(msg, '\n');
		if (!p) {
			firstline = msg;
			restofmsg = "";
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
		flags = items[3];
		logage = time(NULL) - atoi(items[4]);
		timesincechange[0] = '\0'; p = timesincechange;
		if (logage > 86400) p += sprintf(p, "%d days,", (int) (logage / 86400));
		p += sprintf(p, "%d hours, %d minutes", (int) ((logage % 86400) / 3600), (int) ((logage % 3600) / 60));
		logtime = atoi(items[5]);
		sender = items[9];

		if (items[11] && strlen(items[11])) ackmsg = items[11];
		if (ackmsg) nldecode(ackmsg);

		if (items[12] && strlen(items[12])) dismsg = items[12];
		if (dismsg) nldecode(dismsg);
	}
	else {
		char logfn[PATH_MAX];
		struct stat st;
		int fd;
		char *receivedfromtext = "\nMessage received from ";
		char *statusunchangedtext = "\nStatus unchanged in ";
		char *p, *unchangedstr, *receivedfromstr;
		int n;

		if (source == SRC_BBLOGS) {
			sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), commafy(hostname), service);
		}
		else if (source == SRC_HISTLOGS) {
			char *hostnamedash = strdup(hostname);
			p = hostnamedash; while ((p = strchr(p, '.')) != NULL) *p = '_';
			p = hostnamedash; while ((p = strchr(p, ',')) != NULL) *p = '_';
			sprintf(logfn, "%s/%s/%s/%s", getenv("BBHISTLOGS"), hostnamedash, service, tstamp);
			free(hostnamedash);
			p = tstamp; while ((p = strchr(p, '_')) != NULL) *p = ' ';
			sethostenv_histlog(tstamp);
		}

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
		firstline = log;
		restofmsg = strchr(log, '\n'); 
		if (restofmsg) {
			*restofmsg = '\0';
			restofmsg++;
		}
		else {
			restofmsg = "";
		}

		color = parse_color(log);

		p = strstr(log, "<!-- [flags:"); 
		if (p) {
			p += strlen("<!-- [flags:");
			n = strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
			flags = (char *)malloc(n+1);
			strncpy(flags, p, n);
		}
		else {
			flags = "";
		}

		timesincechange[0] = '\0';
		p = unchangedstr = strstr(restofmsg, statusunchangedtext);
		if (p) {
			p += strlen(statusunchangedtext);
			n = strcspn(p, "\n");
			strncpy(timesincechange, p, n);
			timesincechange[n] = '\0';
		}

		p = receivedfromstr = strstr(restofmsg, receivedfromtext); 
		if (p) {
			p += strlen(receivedfromtext);
			n = strspn(p, "0123456789.");
			sender = (char *)malloc(n);
			strncpy(sender, p, n);
			*(sender+n) = '\0';
		}
		else {
			sender = NULL;
		}

		/* Kill the "Status unchanged ..." and "Message received ..." lines */
		if (unchangedstr) *unchangedstr = '\0';
		if (receivedfromstr) *receivedfromstr = '\0';
	}

	fprintf(stdout, "Content-type: text/html\n\n");

	generate_html_log(hostname, displayname, service, ip, 
		          color, sender, flags, 
		          logtime, timesincechange, 
		          firstline, restofmsg, ackmsg, 
		          (source == SRC_HISTLOGS), stdout);
	return 0;
}
#endif
