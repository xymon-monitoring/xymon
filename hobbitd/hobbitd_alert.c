/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This is the main alert module for bbd_net. It receives alert messages,     */
/* keeps track of active alerts, enable/disable, acks etc., and triggers      */
/* outgoing alerts by calling send_alert().                                   */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

/*
 * Information from the bbd docs about "page" modules:
 *
 *   page
 *   ----
 *   @@page|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime
 *   <message>
 *   @@
 *
 *   @@ack|timestamp|sender|hostname|testname|expiretime
 *   <ack message>
 *   @@
 *
 *   Note that "page" modules get messages whenever the alert-state of a test
 *   changes. I.e. a message is generated whenever a test goes from a color
 *   that is non-alerting to a color that is alerting, or vice versa.
 *
 *   How does the pager know when a test is disabled ? It will get a "page"
 *   message with color=blue, if the old color of the test was in an alert
 *   state. (If it wasn't, the pager module does not need to know that the
 *   test has been disabled). It should then clear any stored info about
 *   active alerts for this host.test combination.
 */

static char rcsid[] = "$Id: hobbitd_alert.c,v 1.6 2004-10-17 06:05:54 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "bbdworker.h"
#include "bbd_alert.h"

static volatile int running = 1;

htnames_t *hostnames = NULL;
htnames_t *testnames = NULL;
activealerts_t *ahead = NULL;


activealerts_t *find_active(char *hostname, char *testname)
{
	htnames_t *hwalk, *twalk;
	activealerts_t *awalk;

	for (hwalk = hostnames; (hwalk && strcmp(hostname, hwalk->name)); hwalk = hwalk->next) ;
	if (hwalk == NULL) return NULL;

	for (twalk = testnames; (twalk && strcmp(testname, twalk->name)); twalk = twalk->next) ;
	if (twalk == NULL) return NULL;

	for (awalk = ahead; (awalk && (awalk->hostname != hwalk) && (awalk->testname != twalk)); awalk=awalk->next) ;
	return awalk;
}


void sig_handler(int signum)
{
	int status;

	switch (signum) {
	  case SIGCHLD: 
		  wait(&status);
		  break;

	  default:
		  running = 0;
		  break;
	}
}

int main(int argc, char *argv[])
{
	char *msg;
	int seq;
	int argi;
	int alertcolors = ( (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE) );

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--alertcolors=", 14) == 0) {
			char *colspec = strchr(argv[argi], '=') + 1;
			int c, ac;
			char *p;

			p = strtok(colspec, ",");
			ac = 0;
			while (p) {
				c = parse_color(p);
				if (c != -1) ac = (ac | (1 << c));
				p = strtok(NULL, ",");
			}

			alertcolors = ac;
		}
	}

	setup_signalhandler("bbd_alert");
	signal(SIGCHLD, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	while (running) {
		char *eoln, *restofmsg;
		char *metadata[20];
		char *p;
		int i;
		char *hostname, *testname;
		struct timeval timeout;
		time_t now;
		int anytogo;
		activealerts_t *awalk, *khead, *tmp;

		timeout.tv_sec = 60; timeout.tv_usec = 0;
		msg = get_bbgend_message("bbd_alert", &seq, &timeout);
		if (msg == NULL) {
			/*
			 * This will happen when one of our children finishes sending off alarms
			 */
			dprintf("Got NULL message, exiting\n");
			continue;
		}

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		*eoln = '\0';
		restofmsg = eoln+1;

		/* 
		 * Now parse the meta-data into elements.
		 * We use our own "gettok()" routine which works
		 * like strtok(), but can handle empty elements.
		 */
		i = 0; 
		p = gettok(msg, "|");
		while (p) {
			metadata[i] = p;
			i++;
			p = gettok(NULL, "|");
		}
		metadata[i] = NULL;

		hostname = metadata[3];
		testname = metadata[4];

		dprintf("Got message from %s:%s\n", hostname, testname);

		if (strncmp(metadata[0], "@@page", 6) == 0) {
			/* @@page|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime */

			int newcolor, newalertstatus;

			dprintf("Incoming alert %s\n", msg);
			awalk = find_active(hostname, testname);
			if (awalk == NULL) {
				htnames_t *hwalk;
				htnames_t *twalk;

				dprintf("New alert\n");
				for (hwalk = hostnames; (hwalk && strcmp(hostname, hwalk->name)); hwalk = hwalk->next) ;
				if (hwalk == NULL) {
					hwalk = (htnames_t *)malloc(sizeof(htnames_t));
					hwalk->name = strdup(hostname);
					hwalk->next = hostnames;
					hostnames = hwalk;
					dprintf("Created new hostname record %s\n", hostname);
				}

				for (twalk = testnames; (twalk && strcmp(testname, twalk->name)); twalk = twalk->next) ;
				if (twalk == NULL) {
					twalk = (htnames_t *)malloc(sizeof(htnames_t));
					twalk->name = strdup(testname);
					twalk->next = testnames;
					testnames = twalk;
					dprintf("Created new testname record %s\n", testname);
				}

				awalk = (activealerts_t *)malloc(sizeof(activealerts_t));
				awalk->hostname = hwalk;
				awalk->testname = twalk;
				awalk->color = 0;
				awalk->pagemessage = NULL;
				awalk->ackmessage = NULL;
				awalk->eventstart = time(NULL);
				awalk->nextalerttime = 0;
				awalk->state = A_DEAD;
				awalk->next = ahead;
				ahead = awalk;
			}

			newcolor = parse_color(metadata[6]);
			newalertstatus = ((alertcolors & (1 << newcolor)) != 0);
			awalk->color = newcolor;
			if (newalertstatus) {
				/* It's in an alert state. */
				awalk->state = A_PAGING;
			}
			else {
				/* Send one "recovered" message out now, then go to A_DEAD */
				awalk->state = A_RECOVERED;
			}

			if (awalk->pagemessage) free(awalk->pagemessage);
			awalk->pagemessage = strdup(restofmsg);
		}
		else if (strncmp(metadata[0], "@@ack", 5) == 0) {
 			/* @@ack|timestamp|sender|hostname|testname|expiretime */

			/*
			 * An ack is handled simply by setting the next
			 * alert-time to when the ack expires.
			 */
			awalk = find_active(hostname, testname);
			if (awalk && (awalk->state == A_PAGING)) {
				awalk->state = A_ACKED;
				awalk->nextalerttime = atoi(metadata[5]);
				if (awalk->ackmessage) free(awalk->ackmessage);
				awalk->ackmessage = strdup(restofmsg);
			}
		}
		else if (strncmp(metadata[0], "@@drophost", 10) == 0) {
			/* @@drophost|timestamp|sender|hostname */
			htnames_t *hwalk;

			for (hwalk = hostnames; (hwalk && strcmp(hostname, hwalk->name)); hwalk = hwalk->next) ;
			for (awalk = ahead; (awalk); awalk = awalk->next) {
				if (awalk->hostname == hwalk) awalk->state = A_DEAD;
			}
		}
		else if (strncmp(metadata[0], "@@droptest", 10) == 0) {
			/* @@droptest|timestamp|sender|hostname|testname */

			awalk = find_active(hostname, testname);
			if (awalk) {
				if (awalk == ahead) {
					ahead = ahead->next;
				}
				else {
					activealerts_t *prev;

					for (prev = ahead; (prev->next != awalk); prev = prev->next) ;
					prev->next = awalk->next;
				}

				if (awalk->pagemessage) free(awalk->pagemessage);
				free(awalk);
			}
		}
		else if (strncmp(metadata[0], "@@renamehost", 12) == 0) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			htnames_t *hwalk;
			char *newhostname = metadata[4];

			for (hwalk = hostnames; (hwalk && strcmp(hostname, hwalk->name)); hwalk = hwalk->next) ;
			if (hwalk) {
				free(hwalk->name);
				hwalk->name = strdup(newhostname);
			}
		}
		else if (strncmp(metadata[0], "@@renametest", 12) == 0) {
			/* @@renametest|timestamp|sender|hostname|oldtestname|newtestname */
			htnames_t *newtest;
			char *newtestname = metadata[5];

			awalk = find_active(hostname, testname);
			if (awalk) {
				for (newtest = testnames; (newtest && strcmp(newtestname, newtest->name)); newtest = newtest->next); 
				if (newtest == NULL) {
					/* The new testname does not exist. */
					newtest = (htnames_t *) malloc(sizeof(htnames_t));
					newtest->name = strdup(newtestname);
					newtest->next = testnames;
					testnames = newtest;
				}
				awalk->testname = newtest;
			}
		}
		else if (strncmp(metadata[0], "@@idle", 12) == 0) {
			/* Timeout */
		}

		/* Loop through the activealerts list and see if anything is pending */
		now = time(NULL); anytogo = 0;
		for (awalk = ahead; (awalk); awalk = awalk->next) {
			if ( ((awalk->nextalerttime <= now) && (awalk->state == A_PAGING)) || 
			     (awalk->state == A_RECOVERED)                                 ||
			     (awalk->state == A_ACKED)                                        ) {
				dprintf("Found pending alert: %s.%s\n", awalk->hostname->name, awalk->testname->name);
				anytogo++;
			}
		}
		dprintf("%d alerts to go\n", anytogo);

		if (anytogo) {
			pid_t childpid;

			load_alertconfig();
			childpid = fork();

			if (childpid == 0) {
				/* The child */
				start_alerts();
				for (awalk = ahead; (awalk); awalk = awalk->next) {
					if ( ((awalk->nextalerttime <= now) && (awalk->state == A_PAGING)) ||
					     (awalk->state == A_RECOVERED)                                 ||
					     (awalk->state == A_ACKED)                                       ) {
						send_alert(awalk);
					}
				}
				finish_alerts();
				/* Child does not continue */
				exit(0);
			}
			else if (childpid > 0) {
				/* The parent updates the alert timestamps */
				for (awalk = ahead; (awalk); awalk = awalk->next) {
					if ((awalk->nextalerttime <= now) && (awalk->state == A_PAGING)) {
						awalk->nextalerttime = next_alert(awalk);
					}
					else if (awalk->state == A_ACKED) {
						/*
						 * Acked alerts go back to state A_PAGING.
						 * The nextalerttime ensures they wont send out alerts
						 * until the ack has expired.
						 */
						awalk->state = A_PAGING;
					}
					else if (awalk->state == A_RECOVERED) {
						awalk->state = A_DEAD;
					}
				}
			}
			else {
				errprintf("Fork failed, cannot send alerts: %s\n", strerror(errno));
			}
		}

		/* 
		 * Cleanup dead events,
		 * All A_DEAD and A_RECOVERED items are deleted.
		 */
		khead = NULL; awalk = ahead;
		while (awalk) {
			if ((awalk == ahead) && (awalk->state != A_PAGING)) {
				/* head of alert chain is going away */

				/* Unlink ahead from the chain ... */
				tmp = ahead;
				ahead = ahead->next;

				/* ... and link it into the kill-list */
				tmp->next = khead;
				khead = tmp;

				/* We're still at the head of the chain. */
				awalk = ahead;
			}
			else if (awalk->next && (awalk->next->state != A_PAGING)) {
				/* Unlink awalk->next from the chain ... */
				tmp = awalk->next;
				awalk->next = tmp->next;

				/* ... and link it into the kill-list */
				tmp->next = khead;
				khead = tmp;

				/* awalk stays unchanged */
			}
			else {
				awalk = awalk->next;
			}
		}

		/* khead now holds a list of dead items */
		while (khead) {
			tmp = khead;
			khead = khead->next;

			if (tmp->pagemessage) free(tmp->pagemessage);
			if (tmp->ackmessage) free(tmp->ackmessage);
			free(tmp);
		}
	}

	return 0;
}

