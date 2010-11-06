/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This is a xymond worker module, it should be run off xymond_channel.       */
/*                                                                            */
/* This module implements the traditional Big Brother filebased storage of    */
/* incoming status messages to the bbvar/logs/, bbvar/data/, bb/www/notes/    */
/* and bbvar/disabled/ directories.                                           */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <libgen.h>

#include "libxymon.h"

#include "xymond_worker.h"

static char *multigraphs = ",disk,inode,qtree,quotas,snapshot,TblSpace,if_load,";
static int locatorbased = 0;

enum role_t { ROLE_STATUS, ROLE_DATA, ROLE_NOTES, ROLE_ENADIS};

void update_file(char *fn, char *mode, char *msg, time_t expire, char *sender, time_t timesincechange, int seq)
{
	FILE *logfd;
	char tmpfn[PATH_MAX];
	char *p;

	MEMDEFINE(tmpfn);

	dbgprintf("Updating seq %d file %s\n", seq, fn);

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

	MEMUNDEFINE(tmpfn);
}

void update_htmlfile(char *fn, char *msg, 
		     char *hostname, char *service, int color, int flapping,
		     char *sender, char *flags,
		     time_t logtime, time_t timesincechange,
		     time_t acktime, char *ackmsg,
                     time_t disabletime, char *dismsg)
{
	FILE *output;
	char *tmpfn;
	char *firstline, *restofmsg;
	char *displayname = hostname;
	char *ip = "";
	char timestr[100];

	MEMDEFINE(timestr);

	tmpfn = (char *) malloc(strlen(fn)+5);
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
			color, flapping, sender, flags,
			logtime, timestr,
			firstline, restofmsg, NULL,
			acktime, ackmsg, NULL,
			disabletime, dismsg,
			0, 1, 0, locatorbased, multigraphs, NULL, 
			NULL, NULL, NULL,
			0,
			output);

		fclose(output);
		rename(tmpfn, fn);
	}

	xfree(tmpfn);
	MEMUNDEFINE(timestr);
}

void update_enable(char *fn, time_t expiretime)
{
	time_t now = getcurrenttime(NULL);

	dbgprintf("Enable/disable file %s, time %d\n", fn, (int)expiretime);

	if (expiretime <= now) {
		if (unlink(fn) != 0) {
			errprintf("Could not remove disable-file '%s':%s\n", fn, strerror(errno));
		}
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

	ckey = (char *)malloc(strlen(key) + 3);
	sprintf(ckey, ",%s,", key);
	p = strstr(wanted, ckey);
	xfree(ckey);

	if (p) dbgprintf("wantedtest: Found '%s' at '%s'\n", key, p);
	else dbgprintf("wantedtest: '%s' not found\n", key);

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
	enum msgchannels_t chnid = C_STATUS;
	int argi;
	int seq;
	int running = 1;

	/* Dont save the error buffer */
	save_errbuf = 0;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--status") == 0) {
			role = ROLE_STATUS;
			chnid = C_STATUS;
			if (!filedir) filedir = xgetenv("XYMONRAWSTATUSDIR");
		}
		else if (strcmp(argv[argi], "--html") == 0) {
			role = ROLE_STATUS;
			chnid = C_STATUS;
			if (!htmldir) htmldir = xgetenv("XYMONHTMLSTATUSDIR");
		}
		else if (strcmp(argv[argi], "--data") == 0) {
			role = ROLE_DATA;
			chnid = C_DATA;
			if (!filedir) filedir = xgetenv("XYMONDATADIR");
		}
		else if (strcmp(argv[argi], "--notes") == 0) {
			role = ROLE_NOTES;
			chnid = C_NOTES;
			if (!filedir) filedir = xgetenv("XYMONNOTESDIR");
		}
		else if (strcmp(argv[argi], "--enadis") == 0) {
			role = ROLE_ENADIS;
			chnid = C_ENADIS;
			if (!filedir) filedir = xgetenv("XYMONDISABLEDDIR");
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
			onlytests = (char *)malloc(3 + strlen(p));
			sprintf(onlytests, ",%s,", p);
		}
		else if (argnmatch(argv[argi], "--multigraphs=")) {
			char *p = strchr(argv[argi], '=');
			multigraphs = (char *)malloc(strlen(p+1) + 3);
			sprintf(multigraphs, ",%s,", p+1);
		}
		else if (argnmatch(argv[argi], "--locator=")) {
			char *p = strchr(argv[argi], '=');
			locator_init(p+1);
			locatorbased = 1;
		}
	}

	if (filedir == NULL) {
		errprintf("No directory given, aborting\n");
		return 1;
	}

	/* For picking up lost children */
	setup_signalhandler("xymond_filestore");
	signal(SIGPIPE, SIG_DFL);

	if (onlytests) dbgprintf("Storing tests '%s' only\n", onlytests);
	else dbgprintf("Storing all tests\n");

	while (running) {
		char *metadata[20] = { NULL, };
		char *statusdata = "";
		char *p;
		int metacount;
		char *hostname, *testname;
		time_t expiretime = 0;
		char logfn[PATH_MAX];

		MEMDEFINE(logfn);

		msg = get_xymond_message(chnid, "filestore", &seq, NULL);
		if (msg == NULL) {
			running = 0;
			MEMUNDEFINE(logfn);
			continue;
		}

		p = strchr(msg, '\n'); 
		if (p) {
			*p = '\0'; 
			statusdata = p+1;
		}

		metacount = 0;
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < 20)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}

		if ((role == ROLE_STATUS) && (metacount >= 14) && (strncmp(metadata[0], "@@status", 8) == 0)) {
			/* @@status|timestamp|sender|origin|hostname|testname|expiretime|color|testflags|prevcolor|changetime|ackexpiretime|ackmessage|disableexpiretime|disablemessage|clientmsgtstamp|flapping */
			int ltime, flapping = 0;
			time_t logtime = 0, timesincechange = 0, acktime = 0, disabletime = 0;

			hostname = metadata[4];
			testname = metadata[5];
			if (!wantedtest(onlytests, testname)) {
				dbgprintf("Status dropped - not wanted\n");
				MEMUNDEFINE(logfn);
				continue;
			}

			sprintf(logfn, "%s/%s.%s", filedir, commafy(hostname), testname);
			expiretime = atoi(metadata[6]);
			statusdata = msg_data(statusdata);
			sscanf(metadata[1], "%d.%*d", &ltime); logtime = ltime;
			timesincechange = logtime - atoi(metadata[10]);
			update_file(logfn, "w", statusdata, expiretime, metadata[2], timesincechange, seq);
			if (htmldir) {
				char *ackmsg = NULL;
				char *dismsg = NULL;
				char htmllogfn[PATH_MAX];

				MEMDEFINE(htmllogfn);

				if (metadata[11]) acktime = atoi(metadata[11]);
				if (metadata[12] && strlen(metadata[12])) ackmsg = metadata[12];
				if (ackmsg) nldecode(ackmsg);

				if (metadata[13]) disabletime = atoi(metadata[13]);
				if (metadata[14] && strlen(metadata[14]) && (disabletime > 0)) dismsg = metadata[14];
				if (dismsg) nldecode(dismsg);

				flapping = (metadata[16] ? (*metadata[16] == '1') : 0);

				sprintf(htmllogfn, "%s/%s.%s.%s", htmldir, hostname, testname, htmlextension);
				update_htmlfile(htmllogfn, statusdata, hostname, testname, parse_color(metadata[7]), flapping,
						     metadata[2], metadata[8], logtime, timesincechange, 
						     acktime, ackmsg,
						     disabletime, dismsg);

				MEMUNDEFINE(htmllogfn);
			}
		}
		else if ((role == ROLE_DATA) && (metacount > 5) && (strncmp(metadata[0], "@@data", 6) == 0)) {
			/* @@data|timestamp|sender|hostname|testname */
			p = hostname = metadata[4]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[5];
			if (!wantedtest(onlytests, testname)) {
				dbgprintf("data dropped - not wanted\n");
				MEMUNDEFINE(logfn);
				continue;
			}

			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			expiretime = 0;
			update_file(logfn, "a", statusdata, expiretime, NULL, -1, seq);
		}
		else if ((role == ROLE_NOTES) && (metacount > 3) && (strncmp(metadata[0], "@@notes", 7) == 0)) {
			/* @@notes|timestamp|sender|hostname */
			hostname = metadata[3];
			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s", basename(filedir), hostname);
			expiretime = 0;
			update_file(logfn, "w", statusdata, expiretime, NULL, -1, seq);
		}
		else if ((role == ROLE_ENADIS) && (metacount > 5) && (strncmp(metadata[0], "@@enadis", 8) == 0)) {
			/* @@enadis|timestamp|sender|hostname|testname|expiretime */
			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[4];
			expiretime = atoi(metadata[5]);
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			update_enable(logfn, expiretime);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			/* @@drophost|timestamp|sender|hostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;

			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			hostlead = malloc(strlen(hostname) + 2);
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
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			/* @@droptest|timestamp|sender|hostname|testname */
			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[4];
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			unlink(logfn);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;
			char *newhostname;
			char newlogfn[PATH_MAX];

			MEMDEFINE(newlogfn);

			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			hostlead = malloc(strlen(hostname) + 2);
			strcpy(hostlead, hostname); strcat(hostlead, ".");
			p = newhostname = metadata[4]; while ((p = strchr(p, '.')) != NULL) *p = ',';

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

			MEMUNDEFINE(newlogfn);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA) || (role == ROLE_ENADIS)) && (metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			/* @@renametest|timestamp|sender|hostname|oldtestname|newtestname */
			char *newtestname;
			char newfn[PATH_MAX];

			MEMDEFINE(newfn);

			p = hostname = metadata[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = metadata[4];
			newtestname = metadata[5];
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			sprintf(newfn, "%s/%s.%s", filedir, hostname, newtestname);
			rename(logfn, newfn);

			MEMUNDEFINE(newfn);
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
		}
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				freopen(fn, "a", stdout);
				freopen(fn, "a", stderr);
			}
			continue;
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			/* Ignored */
		}
		else {
			errprintf("Dropping message type %s, metacount=%d\n", metadata[0], metacount);
		}

		MEMUNDEFINE(logfn);
	}

	return 0;
}

