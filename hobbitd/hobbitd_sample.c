/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Sample bbd worker module. This module shows how to get messages from one   */
/* of the bbd_net channels. Worker modules subscribe to a channel and can     */
/* use the channel data to implement various types of storage (files, DB) of  */
/* the Big Brother data, or they can implement actions such as alerting via   */
/* pager, e-mail, SNMP trap or .... In fact, a worker module can do anything  */
/* without the master bbd_net daemon having to care about what goes on in the */
/* workers.                                                                   */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_sample.c,v 1.1 2004-10-11 12:34:54 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "bbdworker.h"

int main(int argc, char *argv[])
{
	char *msg;
	int seq;

	/* Handle program options. Maybe we want debug output enabled ? */
	if ((argc > 1) && (strcmp(argv[1], "--debug") == 0)) debug = 1;

	/*
	 * get_bbgend_message() gets the next message from the queue.
	 * The message buffer is allocated and managed by the get_bbgend_message()
	 * routine, so you should NOT try to free or allocate it yourself.
	 *
	 * All messages have a sequence number ranging from 1-999999.
	 *
	 * The first parameter is the name of the calling module; this is
	 * only used for debugging output.
	 * The second parameter must be an (int *), which then receives the
	 * sequence number of the message returned.
	 * 
	 * get_bbgend_message() does not return until a message is ready,
	 * or the channel is closed.
	 */
	while ((msg = get_bbgend_message(argv[0], &seq)) != NULL) {

		/*
		 * Now we have a message. So do something with it.
		 *
		 * The first line of the message is always a '|' separated
		 * list of meta-data about the message. After the first
		 * line, the content varies by channel.
		 */

		char *eoln, *restofmsg;
		char *metadata[20];
		char *p;
		int i;

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		*eoln = '\0';
		restofmsg = eoln+1;

		/* Now parse the meta-data into elements */
		i = 0; 
		p = strtok(msg, "|");
		while (p) {
			metadata[i] = p;
			i++;
			p = strtok(NULL, "|");
		}
		metadata[i] = NULL;

		/*
		 * What happens next is up to the worker module.
		 *
		 * For this sample module, we'll just print out the data we got.
		 */
		printf("Message # %d\n", seq);
		i = 0;
		while (metadata[i]) {
			printf("   Meta #%2d: %s\n", i, metadata[i]);
			i++;
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

	return 0;
}

