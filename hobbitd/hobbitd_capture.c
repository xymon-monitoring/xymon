/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Hobbitd worker module, to capture status messages for a particulr host     */
/* or test, or data type. This is fed from the status- or data-channel, and   */
/* simply logs the data received to a file.                                   */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_capture.c,v 1.3 2006-07-20 16:06:41 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#include "libbbgen.h"
#include "hobbitd_worker.h"

#define MAX_META 20	/* The maximum number of meta-data items in a message */


int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	struct timespec *timeout = NULL;
	pcre *hostexp = NULL;
	pcre *exhostexp = NULL;
	pcre *testexp = NULL;
	pcre *extestexp = NULL;
	pcre *colorexp = NULL;
        const char *errmsg = NULL;
	int errofs = 0;
	FILE *logfd = stdout;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			/*
			 * A global "debug" variable is available. If
			 * it is set, then "dbgprintf()" outputs debug messages.
			 */
			debug = 1;
		}
		else if (strncmp(argv[argi], "--timeout=", 10) == 0) {
			/*
			 * You can have a timeout when waiting for new
			 * messages. If it happens, you will get a "@@idle\n"
			 * message with sequence number 0.
			 * If you dont want a timeout, just pass a NULL for the timeout parameter.
			 */
			timeout = (struct timespec *)(malloc(sizeof(struct timespec)));
			timeout->tv_sec = (atoi(argv[argi]+10));
			timeout->tv_nsec = 0;
		}
		else if (argnmatch(argv[argi], "--hosts=")) {
			char *exp = strchr(argv[argi], '=') + 1;
			hostexp = pcre_compile(exp, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (hostexp == NULL) printf("Invalid expression '%s'\n", exp);
		}
		else if (argnmatch(argv[argi], "--exhosts=")) {
			char *exp = strchr(argv[argi], '=') + 1;
			exhostexp = pcre_compile(exp, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (exhostexp == NULL) printf("Invalid expression '%s'\n", exp);
		}
		else if (argnmatch(argv[argi], "--tests=")) {
			char *exp = strchr(argv[argi], '=') + 1;
			testexp = pcre_compile(exp, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (testexp == NULL) printf("Invalid expression '%s'\n", exp);
		}
		else if (argnmatch(argv[argi], "--extests=")) {
			char *exp = strchr(argv[argi], '=') + 1;
			extestexp = pcre_compile(exp, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (extestexp == NULL) printf("Invalid expression '%s'\n", exp);
		}
		else if (argnmatch(argv[argi], "--colors=")) {
			char *exp = strchr(argv[argi], '=') + 1;
			colorexp = pcre_compile(exp, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (colorexp == NULL) printf("Invalid expression '%s'\n", exp);
		}
		else if (argnmatch(argv[argi], "--outfile=")) {
			char *fn = strchr(argv[argi], '=') + 1;
			logfd = fopen(fn, "a");
			if (logfd == NULL) {
				printf("Cannot open logfile %s: %s\n", fn, strerror(errno));
				logfd = stdout;
			}
		}
		else {
			printf("Unknown option %s\n", argv[argi]);
			printf("Usage: %s [--hosts=EXP] [--tests=EXP] [--exhosts=EXP] [--extests=EXP] [--color=EXP] [--outfile=FILENAME]\n", argv[0]);
			return 0;
		}
	}

	running = 1;
	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		msg = get_hobbitd_message(C_LAST, argv[0], &seq, timeout);
		if (msg == NULL) {
			/*
			 * get_hobbitd_message will return NULL if hobbitd_channel closes
			 * the input pipe. We should shutdown when that happens.
			 */
			running = 0;
			continue;
		}

		/*
		 * Now we have a message. So do something with it.
		 *
		 * The first line of the message is always a '|' separated
		 * list of meta-data about the message. After the first
		 * line, the content varies by channel.
		 */

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}
		else {
			restofmsg = "";
		}

		/* 
		 * Now parse the meta-data into elements.
		 * We use our own "gettok()" routine which works
		 * like strtok(), but can handle empty elements.
		 */
		metacount = 0; 
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		/*
		 * A "shutdown" message is sent when the master daemon
		 * terminates. The child workers should shutdown also.
		 */
		if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			printf("Shutting down\n");
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

		/*
		 * An "idle" message appears when get_hobbitd_message() 
		 * exceeds the timeout setting (ie. you passed a timeout
		 * value). This allows your worker module to perform
		 * some internal processing even though no messages arrive.
		 */
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			printf("Got an 'idle' message\n");
		}

		/*
		 * The "drophost", "droptest", "renamehost" and "renametst"
		 * indicates that a host/test was deleted or renamed. If the
		 * worker module maintains some internal storage (in memory
		 * or persistent file-storage), it should act on these
		 * messages to maintain data consistency.
		 */
		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			printf("Got a 'drophost' message for host '%s'\n", metadata[3]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			printf("Got a 'droptest' message for host '%s' test '%s'\n", metadata[3], metadata[4]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			printf("Got a 'renamehost' message for host '%s' -> '%s'\n", metadata[3], metadata[4]);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			printf("Got a 'renametest' message for host '%s' test '%s' -> '%s'\n", 
				metadata[3], metadata[4], metadata[5]);
		}

		/*
		 * What happens next is up to the worker module.
		 *
		 * For this sample module, we'll just print out the data we got.
		 */
		else {
			int ovector[30];
			int match, i;
			char *hostname = metadata[4];
			char *testname = metadata[5];
			char *color = metadata[7];

			if (hostexp) {
				match = (pcre_exec(hostexp, NULL, hostname, strlen(hostname), 0, 0, ovector, (sizeof(ovector)/sizeof(int))) >= 0);
				if (!match) continue;
			}
			if (exhostexp) {
				match = (pcre_exec(exhostexp, NULL, hostname, strlen(hostname), 0, 0, ovector, (sizeof(ovector)/sizeof(int))) >= 0);
				if (match) continue;
			}
			if (testexp) {
				match = (pcre_exec(testexp, NULL, testname, strlen(testname), 0, 0, ovector, (sizeof(ovector)/sizeof(int))) >= 0);
				if (!match) continue;
			}
			if (exhostexp) {
				match = (pcre_exec(extestexp, NULL, testname, strlen(testname), 0, 0, ovector, (sizeof(ovector)/sizeof(int))) >= 0);
				if (match) continue;
			}
			if (colorexp) {
				match = (pcre_exec(colorexp, NULL, color, strlen(color), 0, 0, ovector, (sizeof(ovector)/sizeof(int))) >= 0);
				if (!match) continue;
			}

			printf("## ");
			for (i=0; (i < metacount); i++) printf("%s ", metadata[i]);
			printf("\n");
			printf("%s\n", restofmsg);
		}
	}

	return 0;
}

