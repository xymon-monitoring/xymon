/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* xymond worker module, to capture status messages for a particulr host      */
/* or test, or data type. This is fed from the status- or data-channel, and   */
/* simply logs the data received to a file.                                   */
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
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#include "libxymon.h"
#include "xymond_worker.h"

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
	int batchtimeout = 30;
	char *batchcmd = NULL;
	strbuffer_t *batchbuf = NULL;
	time_t lastmsgtime = 0;
	int hostnameitem = 4, testnameitem = 5, coloritem = 7;

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
		else if (strcmp(argv[argi], "--client") == 0) {
			hostnameitem = 3;
			testnameitem = 4;
			errprintf("Expecting to be fed from 'client' channel\n");
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
		else if (argnmatch(argv[argi], "--batch-timeout=")) {
			char *p = strchr(argv[argi], '=');
			batchtimeout = atoi(p+1);
			timeout = (struct timespec *)(malloc(sizeof(struct timespec)));
			timeout->tv_sec = batchtimeout;
			timeout->tv_nsec = 0;
		}
		else if (argnmatch(argv[argi], "--batch-command=")) {
			char *p = strchr(argv[argi], '=');
			batchcmd = strdup(p+1);
			batchbuf = newstrbuffer(0);
		}
		else {
			printf("Unknown option %s\n", argv[argi]);
			printf("Usage: %s [--hosts=EXP] [--tests=EXP] [--exhosts=EXP] [--extests=EXP] [--color=EXP] [--outfile=FILENAME] [--batch-timeout=N] [--batch-command=COMMAND]\n", argv[0]);
			return 0;
		}
	}

	signal(SIGCHLD, SIG_IGN);

	running = 1;
	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		msg = get_xymond_message(C_LAST, argv[0], &seq, timeout);
		if (msg == NULL) {
			/*
			 * get_xymond_message will return NULL if xymond_channel closes
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
		 * A "logrotate" message is sent when the Xymon logs are
		 * rotated. The child workers must re-open their logfiles,
		 * typically stdin and stderr - the filename is always
		 * provided in the XYMONDHANNEL_LOGFILENAME environment.
		 */
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				freopen(fn, "a", stdout);
				freopen(fn, "a", stderr);
			}
			continue;
		}

		/*
		 * An "idle" message appears when get_xymond_message() 
		 * exceeds the timeout setting (ie. you passed a timeout
		 * value). This allows your worker module to perform
		 * some internal processing even though no messages arrive.
		 */
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			dbgprintf("Got an 'idle' message\n");
		}

		/*
		 * The "drophost", "droptest", "renamehost" and "renametst"
		 * indicates that a host/test was deleted or renamed. If the
		 * worker module maintains some internal storage (in memory
		 * or persistent file-storage), it should act on these
		 * messages to maintain data consistency.
		 */
		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			dbgprintf("Got a 'drophost' message for host '%s'\n", metadata[3]);
		}
		else if ((metacount > 3) && (strncmp(metadata[0], "@@dropstate", 11) == 0)) {
			dbgprintf("Got a 'dropstate' message for host '%s'\n", metadata[3]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			dbgprintf("Got a 'droptest' message for host '%s' test '%s'\n", metadata[3], metadata[4]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			dbgprintf("Got a 'renamehost' message for host '%s' -> '%s'\n", metadata[3], metadata[4]);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			dbgprintf("Got a 'renametest' message for host '%s' test '%s' -> '%s'\n", 
				metadata[3], metadata[4], metadata[5]);
		}

		/*
		 * Process this message.
		 */
		else {
			int ovector[30];
			int match, i;
			char *hostname = metadata[hostnameitem];
			char *testname = metadata[testnameitem];
			char *color = metadata[coloritem];

			/* See if we should handle the batched messages we've got */
			if (batchcmd && ((lastmsgtime + batchtimeout) < gettimer()) && (STRBUFLEN(batchbuf) > 0)) {
				pid_t childpid = fork();
				int childres = 0;

				if (childpid < 0) {
					/* Fork failed! */
					errprintf("Fork failed: %s\n", strerror(errno));
				}
				else if (childpid == 0) {
					/* Child */
					FILE *cmdpipe = popen(batchcmd, "w");
					if (cmdpipe) {
						/* Write the data to the batch command pipe */
						int n, bytesleft = STRBUFLEN(batchbuf);
						char *outp = STRBUF(batchbuf);

						while (bytesleft) {
							n = fwrite(outp, 1, bytesleft, cmdpipe);
							if (n >= 0) {
								bytesleft -= n;
								outp += n;
							}
							else {
								errprintf("Error while writing data to batch command\n");
								bytesleft = 0;
							}
						}

						childres = pclose(cmdpipe);
					}
					else {
						errprintf("Could not open pipe to batch command '%s'\n", batchcmd);
						childres = 127;
					}

					exit(childres);
				}
				else if (childpid > 0) {
					/* Parent continues */
				}

				clearstrbuffer(batchbuf);
			}


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

			lastmsgtime = gettimer();

			if (batchcmd) {
				addtobuffer(batchbuf, "## ");
				for (i=0; (i < metacount); i++) {
					addtobuffer(batchbuf, metadata[i]);
					addtobuffer(batchbuf, " ");
				}
				addtobuffer(batchbuf, "\n");
				addtobuffer(batchbuf, restofmsg);
				addtobuffer(batchbuf, "\n");
			}
			else {
				fprintf(logfd, "## ");
				for (i=0; (i < metacount); i++) fprintf(logfd, "%s ", metadata[i]);
				fprintf(logfd, "\n");
				fprintf(logfd, "%s\n", restofmsg);
			}
		}
	}

	return 0;
}

