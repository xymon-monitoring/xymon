#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>

#include "bbdworker.h"

/*
 * This is a bbgend worker module.
 * It hangs off the "status", "data" or "notes" channels 
 * (fed via bbd_channel), and saves incoming messages to 
 * physical storage.
 */

enum role_t { ROLE_STATUS, ROLE_DATA, ROLE_NOTES};

void update_file(char *fn, char *mode, char *msg, time_t expire)
{
	FILE *logfd;

	logfd = fopen(fn, mode);
	fwrite(msg, strlen(msg), 1, logfd);
	fclose(logfd);

	if (expire) {
		struct utimbuf logtime;
		logtime.actime = logtime.modtime = expire;
		utime(fn, &logtime);
	}
}

int main(int argc, char *argv[])
{
	char *filedir = NULL;
	char *msg;
	enum role_t role = ROLE_STATUS;
	int argi;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--status") == 0) {
			role = ROLE_STATUS;
			if (!filedir) filedir = getenv("BBLOGS");
		}
		else if (strcmp(argv[argi], "--data") == 0) {
			role = ROLE_DATA;
			if (!filedir) filedir = getenv("BBDATA");
		}
		else if (strcmp(argv[argi], "--notes") == 0) {
			role = ROLE_NOTES;
			if (!filedir) filedir = getenv("BBNOTES");
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--dir=", 6) == 0) {
			filedir = strchr(argv[argi], '=')+1;
		}
	}

	if (filedir == NULL) {
		errprintf("No directory given, aborting\n");
		return 1;
	}

	while ((msg = get_bbgend_message()) != NULL) {
		char *items[20] = { NULL, };
		char *statusdata = "";
		char *p;
		int icount;
		char *hostname, *testname;
		time_t expiretime = 0;
		char logfn[MAX_PATH];

		p = strchr(msg, '\n'); 
		if (p) {
			*p = '\0'; 
			statusdata = p+1;
		}

		p = strtok(msg, "|"); icount = 0;
		while (p && (icount < 20)) {
			items[icount++] = p;
			p = strtok(NULL, "|");
		}

		if ((role == ROLE_STATUS) && (strcmp(items[0], "@@status") == 0)) {
			/* @@status|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime|ackexpiretime|ackmessage|disableexpiretime|disablemessage */
			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			expiretime = atoi(items[5]);
			statusdata = msg_data(statusdata);
			update_file(logfn, "w", statusdata, expiretime);
		}
		else if ((role == ROLE_DATA) && (strcmp(items[0], "@@data")) == 0) {
			/* @@data|timestamp|sender|hostname|testname */
			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			expiretime = 0;
			update_file(logfn, "a", statusdata, expiretime);
		}
		else if ((role == ROLE_NOTES) && (strcmp(items[0], "@@notes") == 0)) {
			/* @@notes|timestamp|sender|hostname */
			hostname = items[3];
			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s", filedir, hostname);
			expiretime = 0;
			update_file(logfn, "w", statusdata, expiretime);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA)) && (strcmp(items[0], "@@drophost") == 0)) {
			/* @@drophost|timestamp|sender|hostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;

			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
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

			free(hostlead);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA)) && (strcmp(items[0], "@@droptest") == 0)) {
			/* @@droptest|timestamp|sender|hostname|testname */
			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			unlink(logfn);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA)) && (strcmp(items[0], "@@renamehost") == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			DIR *dirfd;
			struct dirent *de;
			char *hostlead;
			char *newhostname;
			char newlogfn[MAX_PATH];

			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			hostlead = malloc(strlen(hostname) + 2);
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
			free(hostlead);
		}
		else if (((role == ROLE_STATUS) || (role == ROLE_DATA)) && (strcmp(items[0], "@@renametest") == 0)) {
			/* @@renametest|timestamp|sender|hostname|oldtestname|newtestname */
			char *newtestname;
			char newfn[MAX_PATH];

			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			newtestname = items[5];
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			sprintf(newfn, "%s/%s.%s", filedir, hostname, newtestname);
			rename(logfn, newfn);
		}
	}

	return 0;
}

