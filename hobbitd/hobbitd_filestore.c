/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is a hobbitd worker module, it should be run off hobbitd_channel.     */
/*                                                                            */
/* This module implements the traditional Big Brother filebased storage of    */
/* incoming status messages to the bbvar/logs/, bbvar/data/, bb/www/notes/    */
/* and bbvar/disabled/ directories.                                           */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_filestore.c,v 1.28 2005-01-15 17:38:28 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>

#include "libbbgen.h"

#include "hobbitd_worker.h"

enum role_t { ROLE_STATUS, ROLE_DATA, ROLE_NOTES, ROLE_ENADIS};

void update_file(char *fn, char *mode, char *msg, time_t expire, char *sender, time_t timesincechange, int seq)
{
	FILE *logfd;
	char tmpfn[PATH_MAX];
	char *p;

	dprintf("Updating seq %d file %s\n", seq, fn);

	p = strrchr(fn, '/');
	if (p) {
		*p = '\0';
		sprintf(tmpfn, "%s/.%s", fn, p+1);
		*p = '/';
	}
	else {
		sprintf(tmpfn, ".%s", fn);
	}

	logfd = fopen(tmpfn, mode);
	fwrite(msg, strlen(msg), 1, logfd);
	if (sender) fprintf(logfd, "\n\nMessage received from %s\n", sender);
	if (timesincechange >= 0) {
		char timestr[100];
		char *p = timestr;
		if (timesincechange > 86400) p += sprintf(p, "%ld days, ", (timesincechange / 86400));
		p += sprintf(p, "%ld hours, %ld minutes", 
				((timesincechange % 86400) / 3600), ((timesincechange % 3600) / 60));
		fprintf(logfd, "Status unchanged in %s\n", timestr);
	}
	fclose(logfd);

	if (expire) {
		struct utimbuf logtime;
		logtime.actime = logtime.modtime = expire;
		utime(tmpfn, &logtime);
	}

	rename(tmpfn, fn);
}

void update_htmlfile(char *fn, char *msg, 
		     char *hostname, char *service, int color,
		     char *sender, char *flags,
		     time_t logtime, time_t timesincechange,
		     char *ackmsg)
{
	FILE *output;
	char *tmpfn;
	char *firstline, *restofmsg;
	char *displayname = hostname;
	char *ip = "";
	char timestr[100];

	tmpfn = (char *) xmalloc(strlen(fn)+5);
	sprintf(tmpfn, "%s.tmp", fn);
	output = fopen(tmpfn, "w");

	if (output) {
		firstline = msg;
		restofmsg = strchr(msg, '\n');
		if (restofmsg) {
			*restofmsg = '\0';
			restofmsg++;
		}
		else {
			restofmsg = "";
		}

		if (timesincechange >= 0) {
			char *p = timestr;
			if (timesincechange > 86400) p += sprintf(p, "%ld days, ", (timesincechange / 86400));
			p += sprintf(p, "%ld hours, %ld minutes", 
					((timesincechange % 86400) / 3600), ((timesincechange % 3600) / 60));
		}

		generate_html_log(hostname, displayname, service, ip,
			color, sender, flags,
			logtime, timestr,
			firstline, restofmsg, ackmsg,
			0, 1, 0, 1, output);

		fclose(output);
		rename(tmpfn, fn);
	}

	xfree(tmpfn);
}

void update_enable(char *fn, time_t expiretime)
{
	dprintf("Enable/disable file %s, time %d\n", fn, (int)expiretime);

	if (expiretime == 0) {
		unlink(fn);
	}
	else {
		FILE *enablefd;
		struct utimbuf logtime;

		enablefd = fopen(fn, "w");
		if (enablefd) {
			fclose(enablefd);
		}

		logtime.actime = logtime.modtime = expiretime;
		utime(fn, &logtime);
	}
}

static int wantedtest(char *wanted, char *key)
{
	char *p, *ckey;

	if (wanted == NULL) return 1;

	ckey = (char *)xmalloc(strlen(key) + 3);
	sprintf(ckey, ",%s,", key);
	p = strstr(wanted, ckey);
	xfree(ckey);

	return (p != NULL);
}

int main(int argc, char *argv[])
{
	char *filedir = NULL;
	char *htmldir = NULL;
	char *htmlextension = "html";
	char *onlytests = NULL;
	char *msg;
	enum role_t role = ROLE_STATUS;
	int argi;
	int seq;
	int running = 1;

	/* Dont save the error buffer */
	save_errbuf = 0;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--status") == 0) {
			role = ROLE_STATUS;
			if (!filedir) filedir = getenv("BBLOGS");
		}
		else if (strcmp(argv[argi], "--html") == 0) {
			role = ROLE_STATUS;
			if (!htmldir) htmldir = getenv("BBHTML");
		}
		else if (strcmp(argv[argi], "--data") == 0) {
			role = ROLE_DATA;
			if (!filedir) filedir = getenv("BBDATA");
		}
		else if (strcmp(argv[argi], "--notes") == 0) {
			role = ROLE_NOTES;
			if (!filedir) filedir = getenv("BBNOTES");
		}
		else if (strcmp(argv[argi], "--enadis") == 0) {
			role = ROLE_ENADIS;
			if (!filedir) filedir = getenv("BBDISABLED");
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--dir=")) {
			filedir = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--htmldir=")) {
			htmldir = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--htmlext=")) {
			htmlextension = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--only=")) {
			char *p = strchr(argv[argi], '=') + 1;
			onlytests = (char *)xmalloc(3 + strlen(p));
			sprintf(onlytests, ",%s,", p);
		}
	}

	if (filedir == NULL) {
		errprintf("No directory given, aborting\n");
		return 1;
	}

	/* For picking up lost children */
	setup_signalhandler("hobbitd_filestore");
	signal(SIGPIPE, SIG_DFL);

	while (running) {
		char *items[20] = { NULL, };
		char *statusdata = "";
		char *p;
		int metacount;
		char *hostname, *testname;
		time_t expiretime = 0;
		char logfn[PATH_MAX];

		msg = get_hobbitd_message("filestore", &seq, NULL);
		if (msg == NULL) {
			running = 0;
			continue;
		}

		p = strchr(msg, '\n'); 
		if (p) {
			*p = '\0'; 
			statusdata = p+1;
		}

		p = gettok(msg, "|"); metacount = 0;
		while (p && (metacount < 20)) {
			items[metacount++] = p;
			p = gettok(NULL, "|");
		}

		if ((role == ROLE_STATUS) && (metacount >= 14) && (strncmp(items[0], "@@status", 8) == 0)) {
			/* @@status|timestamp|sender|origin|hostname|testname|expiretime|color|testflags|prevcolor|changetime|ackexpiretime|ackmessage|disableexpiretime|disablemessage */
			int logtime, timesincechange;

			hostname = items[4];
			testname = items[5];
			if (!wantedtest(onlytests, testname)) continue;

			sprintf(logfn, "%s/%s.%s", filedir, commafy(hostname), testname);
			expiretime = atoi(items[6]);
			statusdata = msg_data(statusdata);
			sscanf(items[1], "%d.%*d", &logtime);
			timesincechange = logtime - atoi(items[10]);
			update_file(logfn, "w", statusdata, expiretime, items[2], timesincechange, seq);
			if (htmldir) {
				char *ackmsg = NULL;
				char htmllogfn[PATH_MAX];

				if (items[12] && strlen(items[12])) ackmsg = items[12];
				if (ackmsg) nldecode(ackmsg);

				sprintf(htmllogfn, "%s/%s.%s.%s", htmldir, hostname, testname, htmlextension);
				update_htmlfile(htmllogfn, statusdata, hostname, testname, parse_color(items[7]),
						     items[2], items[8], logtime, timesincechange, ackmsg);
			}
		}
		else if ((role == ROLE_DATA) && (metacount > 5) && (strncmp(items[0], "@@data", 6) == 0)) {
			/* @@data|timestamp|sender|hostname|testname */
			p = hostname = items[4]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[5];
			if (!wantedtest(onlytests, testname)) continue;

			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			expiretime = 0;
			update_file(logfn, "a", statusdata, expiretime, NULL, -1, seq);
		}
		else if ((role == ROLE_NOTES) && (metacount > 3) && (strncmp(items[0], "@@notes", 7) == 0)) {
			/* @@notes|timestamp|sender|hostname */
			hostname = items[3];
			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s", filedir, hostname);
			expiretime = 0;
			update_file(logfn, "w", statusdata, expiretime, NULL, -1, seq);
		}
		else if ((role == ROLE_ENADIS) && (metacount > 5) && (strncmp(items[0], "@@enadis", 8) == 0)) {
			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			expiretime = atoi(items[5]);
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			update_enable(logfn, expiretime);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 3) && (strncmp(items[0], "@@drophost", 10) == 0)) {
			/* @@drophost|timestamp|sender|hostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;

			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			hostlead = xmalloc(strlen(hostname) + 2);
			strcpy(hostlead, hostname); strcat(hostlead, ".");

			dirfd = opendir(filedir);
			if (dirfd) {
				while ( (de = readdir(dirfd)) != NULL) {
					if (strncmp(de->d_name, hostlead, strlen(hostlead)) == 0) {
						sprintf(logfn, "%s/%s", filedir, de->d_name);
						unlink(logfn);
					}
				}
				closedir(dirfd);
			}

			xfree(hostlead);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 4) && (strncmp(items[0], "@@droptest", 10) == 0)) {
			/* @@droptest|timestamp|sender|hostname|testname */
			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			unlink(logfn);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 4) && (strncmp(items[0], "@@renamehost", 12) == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;
			char *newhostname;
			char newlogfn[PATH_MAX];

			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			hostlead = xmalloc(strlen(hostname) + 2);
			strcpy(hostlead, hostname); strcat(hostlead, ".");
			p = newhostname = items[4]; while ((p = strchr(p, '.')) != NULL) *p = ',';

			dirfd = opendir(filedir);
			if (dirfd) {
				while ( (de = readdir(dirfd)) != NULL) {
					if (strncmp(de->d_name, hostlead, strlen(hostlead)) == 0) {
						char *testname = strchr(de->d_name, '.');
						sprintf(logfn, "%s/%s", filedir, de->d_name);
						sprintf(newlogfn, "%s/%s%s", filedir, newhostname, testname);
						rename(logfn, newlogfn);
					}
				}
				closedir(dirfd);
			}
			xfree(hostlead);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 5) && (strncmp(items[0], "@@renametest", 12) == 0)) {
			/* @@renametest|timestamp|sender|hostname|oldtestname|newtestname */
			char *newtestname;
			char newfn[PATH_MAX];

			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			newtestname = items[5];
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			sprintf(newfn, "%s/%s.%s", filedir, hostname, newtestname);
			rename(logfn, newfn);
		}
		else if (strncmp(items[0], "@@shutdown", 10) == 0) {
			running = 0;
		}
		else {
			errprintf("Dropping message type %s, metacount=%d\n", items[0], metacount);
		}
	}

	return 0;
}

