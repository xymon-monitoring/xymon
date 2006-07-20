/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Sample hobbitd worker module. This module shows how to get messages from   */
/* one of the hobbitd channels. Worker modules subscribe to a channel and can */
/* use the channel data to implement various types of storage (files, DB) of  */
/* the Hobbit data, or they can implement actions such as alerting via        */
/* pager, e-mail, SNMP trap or .... In fact, a worker module can do anything  */
/* without the master hobbit daemon having to care about what goes on in the  */
/* workers.                                                                   */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_sample.c,v 1.19 2006-07-20 16:06:41 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

#include "libbbgen.h"
#include "hobbitd_worker.h"

#define MAX_META 20	/* The maximum number of meta-data items in a message */


int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	struct timeval *timeout = NULL;

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
			timeout = (struct timeval *)(malloc(sizeof(struct timeval)));
			timeout->tv_sec = (atoi(argv[argi]+10));
			timeout->tv_usec = 0;
		}
	}

	/*
	 * An "errprintf" routine prints an error message to stderr, including a timestamp.
	 * The error-messages are saved in a string variable, unless you disable that. You
	 * should do so for long-lived processes.
	 */
	save_errbuf = 0;

	/*
	 * If your worker application fork()'s child processes, then
	 * you should ignore or handle SIGCHLD properly, both to avoid
	 * zombie's, and to prevent the SIGCHLD signal from interfering
	 * with the reading of messages from the input pipe.
	 */
	signal(SIGCHLD, SIG_IGN);

	running = 1;
	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		/*
		 * get_hobbitd_message() gets the next message from the queue.
		 * The message buffer is allocated and managed by the get_hobbitd_message()
		 * routine, so you should NOT try to free or allocate it yourself.
		 *
		 * All messages have a sequence number ranging from 1-999999.
		 *
		 *
		 * The first paramater is the channel you're handling - this is used
		 * to determine the needed input buffer size. If you dont know what
		 * channel you're handling, use C_LAST and you'll get a buffer large
		 * enough for the largest channel.
		 *
		 * The second parameter is the name of the calling module; this is
		 * only used for debugging output.
		 *
		 * The third parameter must be an (int *), which then receives the
		 * sequence number of the message returned.
		 *
		 * The fourth parameter is optional; you can pass a filled-in (struct
		 * timeval) here, which then defines the maximum time get_hobbitd_message()
		 * will wait for a new message. get_hobbitd_message() does not modify
		 * the content of the timeout parameter.
		 * 
		 *
		 * get_hobbitd_message() does not return until a message is ready,
		 * or the timeout setting expires, or the channel is closed.
		 */

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
			int i;

			printf("Message # %d received at %d\n", seq, (int)time(NULL));
			for (i=0; (metadata[i]); i++) {
				printf("   Meta #%2d: %s\n", i, metadata[i]);
			}
			printf("\n");

			printf("   Rest of message\n");
			p = restofmsg;
			while (p) {
				eoln = strchr(p, '\n');
				if (eoln) *eoln = '\0';
	
				printf("\t'%s'\n", p);
				if (eoln) p = eoln+1; else p = NULL;
			}
			printf("   >>> End of message <<<\n");
		}
	}

	return 0;
}

