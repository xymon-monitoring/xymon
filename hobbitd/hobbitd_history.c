#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "bbdworker.h"

/*
 * This is a bbgend worker module.
 * It hangs off the "stachg" channel (fed via bbd_channel), 
 * and updates the standard BB history files.
 */

int main(int argc, char *argv[])
{
	char *histdir = NULL;
	char *histlogdir = NULL;
	char *msg;
	int argi;
	int save_allevents = 1;
	int save_hostevents = 1;
	int save_statusevents = 1;
	int save_histlogs = 1;
	FILE *alleventsfd = NULL;

	if (getenv("BBALLHISTLOG")) save_allevents = (strcmp(getenv("BBALLHISTLOG"), "TRUE") == 0);
	if (getenv("BBHOSTHISTLOG")) save_hostevents = (strcmp(getenv("BBHOSTHISTLOG"), "TRUE") == 0);
	if (getenv("SAVESTATUSLOG")) save_histlogs = (strcmp(getenv("SAVESTATUSLOG"), "TRUE") == 0);

	for (argi = 1; (argi < argc); argi++) {
		if (strncmp(argv[argi], "--histdir=", 10) == 0) {
			histdir = strchr(argv[argi], '=')+1;
		}
		else if (strncmp(argv[argi], "--histlogdir=", 10) == 0) {
			histlogdir = strchr(argv[argi], '=')+1;
		}
	}

	if (getenv("BBHIST") && (histdir == NULL)) {
		histdir = strdup(getenv("BBHIST"));
	}
	if (histdir == NULL) {
		errprintf("No history directory given, aborting\n");
		return 1;
	}

	if (save_histlogs && (histlogdir == NULL) && getenv("BBHISTLOGS")) {
		histlogdir = strdup(getenv("BBHISTLOGS"));
	}
	if (save_histlogs && (histlogdir == NULL)) {
		errprintf("No history-log directory given, aborting\n");
		return 1;
	}

	if (save_allevents) {
		char alleventsfn[MAX_PATH];
		sprintf(alleventsfn, "%s/allevents", histdir);
		alleventsfd = fopen(alleventsfn, "a");
		if (alleventsfd == NULL) {
			errprintf("Cannot open the all-events file '%s'\n", alleventsfn);
		}
		setlinebuf(alleventsfd);
	}

	while ((msg = get_bbgend_message()) != NULL) {
		char *items[20] = { NULL, };
		int icount;
		char *p;
		char *statusdata;
		char *hostname, *hostnamecommas, *testname;
		time_t tstamp, lastchg;
		int newcolor, oldcolor;
		char statuslogfn[MAX_PATH];
		struct stat st;
		int logexists;
		struct tm *tstamptm;
		char newcol2[3];
		char oldcol2[3];
		int trend;

		/* @@stachg|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime */
		p = strchr(msg, '\n'); *p = '\0'; statusdata = msg_data(p+1);
		p = strtok(msg, "|"); icount = 0;
		while (p && (icount < 20)) {
			items[icount++] = p;
			p = strtok(NULL, "|");
		}
		sscanf(items[1], "%d.%*d", (int *)&tstamp);
		tstamptm = localtime(&tstamp);
		hostname = items[3];
		testname = items[4];
		newcolor = parse_color(items[6]);
		oldcolor = parse_color(items[7]);
		lastchg = atoi(items[8]);

		p = hostnamecommas = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = ',';

		sprintf(statuslogfn, "%s/%s.%s", histdir, hostnamecommas, testname);
		logexists = (stat(statuslogfn, &st) == 0);
		if (lastchg == 0) lastchg = st.st_mtime;

		if (save_histlogs) {
			char *hostdash;
			char fname[MAX_PATH];
			FILE *histlogfd;

			p = hostdash = strdup(hostname);
			while ((p = strchr(p, '.')) != NULL) *p = '_';
			sprintf(fname, "%s/%s", histlogdir, hostdash);
			mkdir(fname, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
			p = fname + sprintf(fname, "%s/%s/%s", histlogdir, hostdash, testname);
			mkdir(fname, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

			p += strftime(p, sizeof(fname)-(p-fname), "/%a_%b_", tstamptm);
			p += sprintf(p, "%d", tstamptm->tm_mday);
			p += strftime(p, sizeof(fname)-(p-fname), "_%H:%M:%S_%Y", tstamptm);
			histlogfd = fopen(fname, "w");
			if (histlogfd) {
				fwrite(statusdata, strlen(statusdata), 1, histlogfd);
				fclose(histlogfd);
			}
			else {
				errprintf("Cannot create histlog file '%s' : %s\n", fname, strerror(errno));
			}
			free(hostdash);
		}

		strncpy(oldcol2, ((oldcolor >= 0) ? colorname(oldcolor) : "-"), 2);
		strncpy(newcol2, colorname(newcolor), 2);
		newcol2[2] = oldcol2[2] = '\0';

		if (oldcolor == -1)           trend = -1;	/* we dont know how bad it was */
		else if (newcolor > oldcolor) trend = 2;	/* It's getting worse */
		else if (newcolor < oldcolor) trend = 1;	/* It's getting better */
		else                          trend = 0;	/* Shouldn't happen ... */

		if (oldcolor == -1) lastchg = tstamp;

		if (save_allevents) {
			fprintf(alleventsfd, "%s %s %d %d %d %s %s %d\n",
				hostname, testname, (int)tstamp, (int)lastchg, (int)(tstamp - lastchg),
				newcol2, oldcol2, trend);
		}

		if (save_hostevents) {
			char hostlogfn[MAX_PATH];
			FILE *hostlogfd;

			sprintf(hostlogfn, "%s/%s", histdir, hostname);
			hostlogfd = fopen(hostlogfn, "a");
			if (hostlogfd) {
				fprintf(hostlogfd, "%s %d %d %d %s %s %d\n",
					testname, (int)tstamp, (int)lastchg, (int)(tstamp - lastchg),
					newcol2, oldcol2, trend);
				fclose(hostlogfd);
			}
			else {
				errprintf("Cannot open host logfile '%s' : %s\n", hostlogfn, strerror(errno));
			}
		}

		if (save_statusevents) {
			char timestamp[40];
			FILE *statuslogfd;

			statuslogfd = fopen(statuslogfn, "a");
			if (statuslogfd) {
				if (logexists) fprintf(statuslogfd, " %d\n", (int)(tstamp - lastchg));

				strftime(timestamp, sizeof(timestamp), "%a %b %e %H:%M:%S %Y", tstamptm);
				fprintf(statuslogfd, "%s %s   %d", timestamp, colorname(newcolor), (int)tstamp);
				fclose(statuslogfd);
			}
			else {
				errprintf("Cannot open status historyfile '%s' : %s\n", statuslogfn, strerror(errno));
			}
		}

	}

	fclose(alleventsfd);
	return 0;
}

