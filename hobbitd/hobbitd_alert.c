/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This is the main alert module for bbgend. It receives alert messages,      */
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
 * Information from the bbgend docs about "page" modules:
 *
 *   page
 *   ----
 *   @@page|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime|location
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

static char rcsid[] = "$Id: hobbitd_alert.c,v 1.21 2004-11-14 14:03:13 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include "libbbgen.h"

#include "bbgend_worker.h"
#include "bbgend_alert.h"

static volatile int running = 1;

htnames_t *hostnames = NULL;
htnames_t *testnames = NULL;
htnames_t *locations = NULL;
activealerts_t *ahead = NULL;

char *statename[] = {
	"paging", "acked", "recovered", "dead"
};

htnames_t *find_name(htnames_t **head, char *name)
{
	htnames_t *walk;

	for (walk = *head; (walk && strcmp(name, walk->name)); walk = walk->next) ;
	if (walk == NULL) {
		walk = (htnames_t *)malloc(sizeof(htnames_t));
		walk->name = strdup(name);
		walk->next = *head;
		*head = walk;
	}

	return walk;
}

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

void save_checkpoint(char *filename)
{
	char *subfn;
	FILE *fd = fopen(filename, "w");
	activealerts_t *awalk;
	unsigned char *pgmsg = "", *ackmsg = "";

	if (fd == NULL) return;

	for (awalk = ahead; (awalk); awalk = awalk->next) {
		fprintf(fd, "%s|%s|%s|%s|%d|%d|%s|",
			awalk->hostname->name, awalk->testname->name, awalk->location->name,
			colorname(awalk->color),
			(int) awalk->eventstart,
			(int) awalk->nextalerttime,
			statename[awalk->state]);
		if (awalk->pagemessage) pgmsg = nlencode(awalk->pagemessage);
		fprintf(fd, "%s|", pgmsg);
		if (awalk->ackmessage) ackmsg = nlencode(awalk->ackmessage);
		fprintf(fd, "%s\n", ackmsg);
	}
	fclose(fd);

	subfn = (char *)malloc(strlen(filename)+5);
	sprintf(subfn, "%s.sub", filename);
	save_state(subfn);
	free(subfn);
}

void load_checkpoint(char *filename)
{
	char *subfn;
	FILE *fd = fopen(filename, "r");
	char l[4*MAXMSG+1024];

	if (fd == NULL) return;

	while (fgets(l, sizeof(l), fd)) {
		char *item[20], *p;
		int i;

		p = strchr(l, '\n'); if (p) *p = '\0';

		i = 0; p = gettok(l, "|");
		while (p && (i < 20)) {
			item[i++] = p;
			p = gettok(NULL, "|");
		}

		if (i > 8) {
			activealerts_t *newalert = (activealerts_t *)malloc(sizeof(activealerts_t));
			newalert->hostname = find_name(&hostnames, item[0]);
			newalert->testname = find_name(&testnames, item[1]);
			newalert->location = find_name(&locations, item[2]);
			newalert->color = parse_color(item[3]);
			newalert->eventstart = (time_t) atoi(item[4]);
			newalert->nextalerttime = (time_t) atoi(item[5]);
			newalert->state = A_PAGING;
			while (strcmp(item[6], statename[newalert->state]) && (newalert->state < A_DEAD)) 
				newalert->state++;
			newalert->pagemessage = newalert->ackmessage = NULL;
			nldecode(item[7]); nldecode(item[8]);
			if (strlen(item[7])) newalert->pagemessage = strdup(item[7]);
			if (strlen(item[8])) newalert->ackmessage = strdup(item[8]);
			newalert->next = ahead;
			ahead = newalert;
		}
	}
	fclose(fd);

	subfn = (char *)malloc(strlen(filename)+5);
	sprintf(subfn, "%s.sub", filename);
	load_state(subfn);
	free(subfn);
}

int main(int argc, char *argv[])
{
	char *msg;
	int seq;
	int argi;
	int alertcolors = ( (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE) );
	char *configfn = NULL;
	char *checkfn = NULL;
	int checkpointinterval = 900;
	time_t nextcheckpoint = 0;

	/* Dont save the error buffer */
	save_errbuf = 0;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--debug")) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--alertcolors=")) {
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
		else if (argnmatch(argv[argi], "--config=")) {
			configfn = strdup(strchr(argv[argi], '=')+1);
		}
		else if (argnmatch(argv[argi], "--checkpoint-file=")) {
			checkfn = strdup(strchr(argv[argi], '=')+1);
		}
		else if (argnmatch(argv[argi], "--checkpoint-interval=")) {
			char *p = strchr(argv[argi], '=') + 1;
			checkpointinterval = atoi(p);
		}
		else if (argnmatch(argv[argi], "--dump-config")) {
			load_alertconfig(configfn, alertcolors);
			dump_alertconfig();
			return 0;
		}
		else {
			errprintf("Unknown option '%s'\n", argv[argi]);
		}
	}

	if (checkfn) {
		load_checkpoint(checkfn);
		nextcheckpoint = time(NULL) + checkpointinterval;
		dprintf("Next checkpoint at %d, interval %d\n", (int) nextcheckpoint, checkpointinterval);
	}

	setup_signalhandler("bbgend_alert");
	/* Need to handle these ourselves, so we can shutdown and save state-info */
	signal(SIGCHLD, sig_handler);
	signal(SIGPIPE, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	while (running) {
		char *eoln, *restofmsg;
		char *metadata[20];
		char *p;
		int metacount;
		char *hostname = NULL, *testname = NULL;
		struct timeval timeout;
		time_t now;
		int anytogo;
		activealerts_t *awalk, *khead, *tmp;

		if (checkfn && (time(NULL) > nextcheckpoint)) {
			dprintf("Saving checkpoint\n");
			nextcheckpoint = time(NULL)+checkpointinterval;
			save_checkpoint(checkfn);
		}

		timeout.tv_sec = 60; timeout.tv_usec = 0;
		msg = get_bbgend_message("bbgend_alert", &seq, &timeout);
		if (msg == NULL) {
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

		/* 
		 * Now parse the meta-data into elements.
		 * We use our own "gettok()" routine which works
		 * like strtok(), but can handle empty elements.
		 */
		metacount = 0; 
		p = gettok(msg, "|");
		while (p && (metacount < 19)) {
			metadata[metacount] = p;
			metacount++;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		if (metacount > 3) hostname = metadata[3];
		if (metacount > 4) testname = metadata[4];


		if ((metacount > 10) && (strncmp(metadata[0], "@@page", 6) == 0)) {
			/* @@page|timestamp|sender|hostname|testname|expiretime|color|prevcolor|changetime|location|cookie */

			int newcolor, newalertstatus, oldalertstatus;

			dprintf("Got page message from %s:%s\n", hostname, testname);
			awalk = find_active(hostname, testname);
			if (awalk == NULL) {
				htnames_t *hwalk = find_name(&hostnames, hostname);
				htnames_t *twalk = find_name(&testnames, testname);
				htnames_t *pwalk = find_name(&locations, metadata[9]);

				awalk = (activealerts_t *)malloc(sizeof(activealerts_t));
				awalk->hostname = hwalk;
				awalk->testname = twalk;
				awalk->location = pwalk;
				awalk->color = 0;
				awalk->cookie = -1;
				awalk->pagemessage = NULL;
				awalk->ackmessage = NULL;
				awalk->eventstart = time(NULL);
				awalk->nextalerttime = 0;
				awalk->state = A_DEAD;
				awalk->next = ahead;
				ahead = awalk;
			}

			newcolor = parse_color(metadata[6]);
			oldalertstatus = ((alertcolors & (1 << awalk->color)) != 0);
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

			if (oldalertstatus != newalertstatus) {
				dprintf("Alert status changed from %d to %d\n", oldalertstatus, newalertstatus);
				clear_interval(awalk);
			}

			awalk->cookie = atoi(metadata[10]);

			if (awalk->pagemessage) free(awalk->pagemessage);
			awalk->pagemessage = strdup(restofmsg);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@ack", 5) == 0)) {
 			/* @@ack|timestamp|sender|hostname|testname|expiretime */

			/*
			 * An ack is handled simply by setting the next
			 * alert-time to when the ack expires.
			 */
			dprintf("Got ack message from %s:%s\n", hostname, testname);
			awalk = find_active(hostname, testname);
			if (awalk && (awalk->state == A_PAGING)) {
				awalk->state = A_ACKED;
				awalk->nextalerttime = atoi(metadata[5]);
				if (awalk->ackmessage) free(awalk->ackmessage);
				awalk->ackmessage = strdup(restofmsg);
			}
		}
		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			/* @@drophost|timestamp|sender|hostname */
			htnames_t *hwalk;

			for (hwalk = hostnames; (hwalk && strcmp(hostname, hwalk->name)); hwalk = hwalk->next) ;
			for (awalk = ahead; (awalk); awalk = awalk->next) {
				if (awalk->hostname == hwalk) awalk->state = A_DEAD;
			}
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
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
		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			htnames_t *hwalk;
			char *newhostname = metadata[4];

			for (hwalk = hostnames; (hwalk && strcmp(hostname, hwalk->name)); hwalk = hwalk->next) ;
			if (hwalk) {
				free(hwalk->name);
				hwalk->name = strdup(newhostname);
			}
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
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
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
			continue;
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			/* Timeout */
		}

		/* Loop through the activealerts list and see if anything is pending */
		now = time(NULL); anytogo = 0;
		for (awalk = ahead; (awalk); awalk = awalk->next) {
			if ((awalk->nextalerttime <= now) && (awalk->state == A_PAGING)) {
				if (awalk->ackmessage) {
					/* An ack has expired, so drop the ack message */
					free(awalk->ackmessage);
					awalk->ackmessage = NULL;
				}
				anytogo++;
			}
			else if ((awalk->state == A_RECOVERED) || (awalk->state == A_ACKED)) { 
				anytogo++;
			}
		}
		dprintf("%d alerts to go\n", anytogo);

		if (anytogo) {
			pid_t childpid;

			load_alertconfig(configfn, alertcolors);
			childpid = fork();

			if (childpid == 0) {
				/* The child */
				start_alerts();
				for (awalk = ahead; (awalk); awalk = awalk->next) {
					if ( ((awalk->nextalerttime <= now) && (awalk->state == A_PAGING)) ||
					     (awalk->state == A_ACKED)                                     ||
					     (awalk->state == A_RECOVERED)                                    ) {
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
				}
			}
			else {
				errprintf("Fork failed, cannot send alerts: %s\n", strerror(errno));
			}
		}

		for (awalk = ahead; (awalk); awalk = awalk->next) {
			switch (awalk->state) {
			  case A_ACKED: 
				  /* This really cannot happen */
				  break;

			  case A_PAGING: 
				  break;

			  case A_RECOVERED: 
			  case A_DEAD: 
				  cleanup_alert(awalk); 
				  awalk->state = A_DEAD; 
				  break;
			}
		}

		/* 
		 * Cleanup events. Items here are either A_DEAD or A_PAGING.
		 * All A_DEAD items are deleted.
		 */
		khead = NULL; awalk = ahead;
		while (awalk) {
			if ((awalk == ahead) && (awalk->state == A_DEAD)) {
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
			else if (awalk->next && (awalk->next->state == A_DEAD)) {
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

	if (checkfn) save_checkpoint(checkfn);
	return 0;
}

