#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>

#include "bbdworker.h"

/*
 * This is a bbgend worker module.
 * It hangs off the "status", "data" or "notes" channels 
 * (fed via bbd_channel), and saves incoming messages to 
 * physical storage.
 */

enum role_t { ROLE_STATUS, ROLE_DATA, ROLE_NOTES};

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
		char *statusdata;
		char *p;
		int icount;
		char *hostname, *testname;
		time_t expiretime = 0;
		char logfn[MAX_PATH];
		FILE *logfd;

		p = strchr(msg, '\n'); *p = '\0'; statusdata = p+1;
		p = strtok(msg, "|"); icount = 0;
		while (p && (icount < 20)) {
			items[icount++] = p;
			p = strtok(NULL, "|");
		}

		switch (role) {
		  case ROLE_STATUS:
			/* @@status|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime|ackexpiretime|ackmessage|disableexpiretime|disablemessage */
			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			expiretime = atoi(items[5]);
			statusdata = msg_data(statusdata);
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			break;

		  case ROLE_DATA:
			/* @@data|timestamp|sender|hostname|testname */
			p = hostname = items[3]; while ((p = strchr(p, '.')) != NULL) *p = ',';
			testname = items[4];
			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s.%s", filedir, hostname, testname);
			expiretime = 0;
			break;

		  case ROLE_NOTES:
			/* @@notes|timestamp|sender|hostname */
			hostname = items[3];
			statusdata = msg_data(statusdata); if (*statusdata == '\n') statusdata++;
			sprintf(logfn, "%s/%s", filedir, hostname);
			expiretime = 0;
			break;
		}

		logfd = fopen(logfn, "w");
		fwrite(statusdata, strlen(statusdata), 1, logfd);
		fclose(logfd);

		if (expiretime) {
			struct utimbuf logtime;
			logtime.actime = logtime.modtime = expiretime;
			utime(logfn, &logtime);
		}
	}

	return 0;
}

