/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This Hobbit worker module saves the client messages that arrive on the     */
/* CLICHG channel, for use when looking at problems with a host.              */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_hostdata.c,v 1.5 2006-07-20 16:06:41 henrik Exp $";

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>

#include "libbbgen.h"
#include "hobbitd_worker.h"

#define MAX_META 20	/* The maximum number of meta-data items in a message */


int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	char *clientlogdir = NULL;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
                if (argnmatch(argv[argi], "--logdir=")) {
			clientlogdir = strchr(argv[argi], '=')+1;
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			/*
			 * A global "debug" variable is available. If
			 * it is set, then "dbgprintf()" outputs debug messages.
			 */
			debug = 1;
		}
	}

	if (clientlogdir == NULL) clientlogdir = xgetenv("CLIENTLOGS");
	if (clientlogdir == NULL) {
		clientlogdir = (char *)malloc(strlen(xgetenv("BBVAR")) + 10);
		sprintf(clientlogdir, "%s/hostdata", xgetenv("BBVAR"));
	}

	save_errbuf = 0;
	setup_signalhandler("hobbitd_hostdata");

	running = 1;
	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		msg = get_hobbitd_message(C_CLICHG, "hobbitd_hostdata", &seq, NULL);
		if (msg == NULL) {
			/*
			 * get_hobbitd_message will return NULL if hobbitd_channel closes
			 * the input pipe. We should shutdown when that happens.
			 */
			running = 0;
			continue;
		}

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}
		else {
			restofmsg = "";
		}

		metacount = 0; 
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		if (strncmp(metadata[0], "@@clichg", 8) == 0) {
			char hostdir[PATH_MAX];
			char fn[PATH_MAX];
			FILE *fd;

			sprintf(hostdir, "%s/%s", clientlogdir, metadata[3]);
			mkdir(hostdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
			sprintf(fn, "%s/%s", hostdir, metadata[4]);
			fd = fopen(fn, "w");
			if (fd == NULL) {
				errprintf("Cannot create file %s: %s\n", fn, strerror(errno));
				continue;
			}
			fwrite(restofmsg, strlen(restofmsg), 1, fd);
			fclose(fd);
		}

		/*
		 * A "shutdown" message is sent when the master daemon
		 * terminates. The child workers should shutdown also.
		 */
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
			continue;
		}

		/*
		 * A "logrotate" message is sent when the Hobbit logs are
		 * rotated. The child workers must re-open their logfiles,
		 * typically stdin and stderr - the filename is always
		 * provided in the HOBBITCHANNEL_LOGFILENAME environment.
		 */
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("HOBBITCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				freopen(fn, "a", stdout);
				freopen(fn, "a", stderr);
			}
			continue;
		}

		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			/* @@drophost|timestamp|sender|hostname */
			char hostdir[PATH_MAX];
			sprintf(hostdir, "%s/%s", clientlogdir, metadata[3]);
			dropdirectory(hostdir, 1);
		}

		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			char oldhostdir[PATH_MAX], newhostdir[PATH_MAX];
			sprintf(oldhostdir, "%s/%s", clientlogdir, metadata[3]);
			sprintf(newhostdir, "%s/%s", clientlogdir, metadata[4]);
			rename(oldhostdir, newhostdir);
		}
	}

	return 0;
}

