/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* xymond worker to distribute enable/disable/drop/rename messages in a       */
/* multi-server active/active setup.                                          */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: xymond_sample.c 6748 2011-09-04 17:24:36Z storner $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

#include "libxymon.h"
#include "xymond_worker.h"

#define MAX_META 20

int peercount = 0;
char **peers = NULL;
char *channelname = NULL;
char *myhostname = NULL;

int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	char newmsg[4096];

	/* Handle program options. */
	libxymon_init(argv[0]);
	for (argi = 1; (argi < argc); argi++) {
		if (strncmp(argv[argi], "--peer=", 7) == 0) {
			char *ip = strchr(argv[argi], '=') + 1;

			if (!peers) {
				peercount = 1;
				peers = (char **)calloc((peercount + 1), sizeof(char *));
				peers[peercount-1] = strdup(ip);
				peers[peercount] = NULL;
			}
			else {
				peercount++;
				peers = (char **)realloc(peers, (peercount + 1)*sizeof(char *));
				peers[peercount-1] = strdup(ip);
				peers[peercount] = NULL;
			}
		}
		else if (strncmp(argv[argi], "--channel=", 10) == 0) {
			channelname = strdup(strchr(argv[argi], '=') + 1);
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}

	}

	if (!peers) {
		errprintf("No peers specified, aborting\n");
		return 1;
	}

	save_errbuf = 0;
	signal(SIGCHLD, SIG_IGN);

	running = 1;
	while (running) {
		char *eoln, *p;
		char *metadata[MAX_META+1];
		int metacount;

		*newmsg = '\0';
		msg = get_xymond_message(C_LAST, argv[0], &seq, NULL);

		/* Split the message in the first line (with meta-data), and the rest. We're only interested in the first line. */
 		eoln = strchr(msg, '\n');
		if (eoln) *eoln = '\0';

		metacount = 0; 
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;


		if ((msg == NULL) || (strncmp(metadata[0], "@@shutdown", 10) == 0)) {
			printf("Shutting down\n");
			running = 0;
			continue;
		}

		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				reopen_file(fn, "a", stdout);
				reopen_file(fn, "a", stderr);
			}
			continue;
		}

		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			snprintf(newmsg, sizeof(newmsg)-1, "drop %s", metadata[3]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			snprintf(newmsg, sizeof(newmsg)-1, "drop %s %s", metadata[3], metadata[4]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			snprintf(newmsg, sizeof(newmsg)-1, "rename %s %s", metadata[3], metadata[4]);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			snprintf(newmsg, sizeof(newmsg)-1, "rename %s %s %s", metadata[3], metadata[4], metadata[5]);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@enadis", 8) == 0)) {
			/* @@enadis|timestamp|sender|hostname|testname|expiretime|message */

			if (strcmp(metadata[5], "0") == 0) {
				snprintf(newmsg, sizeof(newmsg)-1, "enable %s.%s", metadata[3], metadata[4]);
			}
			else {
				long int distime;
				
				/* Disable until OK has time -1; normal disables has a count of minutes */
				distime = (strcmp(metadata[5], "-1") == 0) ? -1 : ((atol(metadata[5]) - time(NULL)) / 60);

				snprintf(newmsg, sizeof(newmsg)-1, "disable %s.%s %ld", metadata[3], metadata[4], distime);
				if (metadata[6] && strlen(metadata[6])) {
					nldecode(metadata[6]);
					sprintf(newmsg + strlen(newmsg), " %s", metadata[6]);
				}
				dbgprintf("Disable: %s\n", newmsg);
			}
		}

		if (strlen(newmsg) > 0) {
			int i;

			for (i = 0; (i < peercount); i++)
				sendmessage(newmsg, peers[i], XYMON_TIMEOUT, NULL);
		}
	}

	return 0;
}

