/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd.c,v 1.12 2004-10-07 14:09:08 henrik Exp $";

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#if !defined(HPUX)              /* HP-UX has select() and friends in sys/types.h */
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ... */
#endif
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbd_net.h"
#include "bbdutil.h"
#include "loadhosts.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };


bbd_hostlist_t *hosts = NULL;		/* The hosts we have reports from */
bbd_testlist_t *tests = NULL;		/* The tests we have seen */
bbd_log_t *logs = NULL;			/* The current logs we have (equivalent to bbvar/logs/ */

#define NOTALK 0
#define RECEIVING 1
#define RESPONDING 2

/* This struct describes an active connection with a BB client */
typedef struct conn_t {
	int sock;			/* Communications socket */
	struct sockaddr_in addr;	/* Client source address */
	unsigned char *buf, *bufp;	/* Message buffer and pointer */
	int buflen, bufsz;		/* Active and maximum length of buffer */
	int doingwhat;			/* Communications state (NOTALK, READING, RESPONDING) */
	struct conn_t *next;
} conn_t;

typedef struct cookie_t {
	unsigned int cookie;
	bbd_log_t *log;
	time_t expires;
	struct cookie_t *next;
} cookie_t;
cookie_t *cookiehead = NULL, *cookietail = NULL;

enum droprencmd_t { CMD_DROPHOST, CMD_DROPTEST, CMD_RENAMEHOST, CMD_RENAMETEST };

static volatile int running = 1;
static volatile int reloadconfig = 1;
static volatile time_t nextcheckpoint = 0;

/* Our channels to worker modules */
bbd_channel_t *statuschn = NULL;	/* Receives full "status" messages */
bbd_channel_t *stachgchn = NULL;	/* Receives brief message about a status change */
bbd_channel_t *pagechn   = NULL;	/* Receives alert messages (triggered from status changes) */
bbd_channel_t *datachn   = NULL;	/* Receives raw "data" messages */
bbd_channel_t *noteschn  = NULL;	/* Receives raw "notes" messages */

#define NO_COLOR (COL_COUNT)
static char *colnames[COL_COUNT+1];
int alertcolors = ( (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE) );
int ghosthandling = -1;
char *checkpointfn = NULL;

void posttochannel(bbd_channel_t *channel, char *channelmarker, 
		   char *msg, char *sender, char *hostname, void *arg, char *readymsg)
{
	bbd_log_t *log;
	char *testname;
	struct sembuf s;
	struct shmid_ds chninfo;
	int clients;
	int n;
	struct timeval tstamp;
	struct timezone tz;
	char *ackmsg = NULL, *dismsg = NULL;

	/* First see how many users are on this channel */
	n = shmctl(channel->shmid, IPC_STAT, &chninfo);
	clients = chninfo.shm_nattch-1;
	if (clients == 0) {
		dprintf("Dropping message - no readers\n");
		return;
	}

	/* If any message is pending, wait until it has been noticed.... */
	s.sem_num = 0;
	s.sem_op = 0;  /* wait until all reads are done (semaphore is 0) */
	s.sem_flg = 0;
	dprintf("Waiting for readers to notice last message\n");
	n = semop(channel->semid, &s, 1);
	dprintf("All readers have seen it (sem 0 is 0)\n");

	/* ... and picked up. */
	s.sem_num = 1;
	s.sem_op = -clients;	/* Each client does an UP, so wait until we get it to 0 */
	s.sem_flg = 0;
	dprintf("Waiting for %d readers to finish with message\n", clients);
	n = semop(channel->semid, &s, 1);
	dprintf("Readers are done with last message\n");

	/* All clear, post the message */
	if (readymsg) {
		strcpy(channel->channelbuf, readymsg);
	}
	else {
		gettimeofday(&tstamp, &tz);
		switch(channel->channelid) {
		  case C_STATUS:
			log = (bbd_log_t *)arg;
			if (log->ackmsg) ackmsg = base64encode(log->ackmsg);
			if (log->dismsg) dismsg = base64encode(log->dismsg);
			sprintf(channel->channelbuf, 
				"@@%s|%d.%06d|%s|%s|%s|%d|%s|%s|%d|%d|%s|%d|%s\n%s\n@@\n", 
				channelmarker, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender, hostname, 
				log->test->testname, (int) log->validtime, 
				colnames[log->color], colnames[log->oldcolor], (int) log->lastchange, 
				(int)log->acktime, (ackmsg ? ackmsg : ""), 
				(int)log->enabletime, (dismsg ? dismsg : ""),
				msg);
			if (dismsg) free(dismsg);
			if (ackmsg) free(ackmsg);
			break;

		  case C_STACHG:
			log = (bbd_log_t *)arg;
			sprintf(channel->channelbuf, 
				"@@%s|%d.%06d|%s|%s|%s|%d|%s|%s|%d\n%s\n@@\n", 
				channelmarker, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender, hostname, 
				log->test->testname, (int) log->validtime, 
				colnames[log->color], colnames[log->oldcolor], (int) log->lastchange, 
				msg);
			break;

		  case C_PAGE:
			log = (bbd_log_t *)arg;
			if (strcmp(channelmarker, "ack") == 0) {
				sprintf(channel->channelbuf, 
					"@@%s|%d.%06d|%s|%s|%s|%d\n%s\n@@\n", 
					channelmarker, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender, hostname, 
					log->test->testname, (int) log->acktime, msg);
			}
			else {
				sprintf(channel->channelbuf, 
					"@@%s|%d.%06d|%s|%s|%s|%d|%s|%s|%d\n%s\n@@\n", 
					channelmarker, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender, hostname, 
					log->test->testname, (int) log->validtime, 
					colnames[log->color], colnames[log->oldcolor], (int) log->lastchange,
					msg);
			}
			break;

		  case C_DATA:
			testname = (char *)arg;
			sprintf(channel->channelbuf, 
				"@@%s|%d.%06d|%s|%s|%s\n%s\n@@\n", 
				channelmarker, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender, hostname, 
				testname, msg);
			break;

		  case C_NOTES:
			sprintf(channel->channelbuf, 
				"@@%s|%d.%06d|%s|%s\n%s\n@@\n", 
				channelmarker, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender, hostname, 
				msg);
			break;
		}
	}

	/* Let the readers know it is there.  */
	s.sem_num = 0;
	s.sem_op = clients;
	s.sem_flg = 0;
	dprintf("Posting message to %d readers\n", clients);
	n = semop(channel->semid, &s, 1);
	dprintf("Message posted\n");

	return;
}


char *msg_data(char *msg)
{
	/* Find the start position of the data following the "status host.test " message */
	char *result;
	
	result = strchr(msg, '.');		/* Hits the '.' in "host.test" */
	if (!result) {
		dprintf("Msg was not what I expected: '%s'\n", msg);
		return msg;
	}

	result += strcspn(result, " \t\n");	/* Skip anything until we see a space, TAB or NL */
	result += strspn(result, " \t");	/* Skip all whitespace */

	return result;
}


int durationvalue(char *dur)
{
	/* 
	 * Calculate a duration, taking special modifiers into consideration.
	 * Return the duration as number of minutes.
	 */

	int result = 0;
	char *p;
	char modifier;
	struct tm *nowtm;
	time_t now;
	
	now = time(NULL);
	nowtm = localtime(&now);

	p = dur + strspn(dur, "0123456789");
	modifier = *p;
	*p = '\0';
	result = atoi(dur);
	*p = modifier;

	switch (modifier) {
	  case 'h': result *= 60; break;	/* hours */
	  case 'd': result *= 1440; break;	/* days */
	  case 'w': result *= 10080; break;	/* weeks */
	  case 'm': 
		    nowtm->tm_mon += result;
		    result = (mktime(nowtm) - now) / 60;
		    break;
	  case 'y': 
		    nowtm->tm_year += result;
		    result = (mktime(nowtm) - now) / 60;
		    break;
	}

	return result;
}


void get_hts(char *msg, char *sender, 
	     bbd_hostlist_t **host, bbd_testlist_t **test, bbd_log_t **log, 
	     int *color, int createhost, int createlog)
{
	/* "msg" contains an incoming message. First list is of the form "KEYWORD host,domain.test COLOR" */
	char *l, *p;
	char *hosttest, *hostname, *testname, *colstr;
	bbd_hostlist_t *hwalk = NULL;
	bbd_testlist_t *twalk = NULL;
	bbd_log_t *lwalk = NULL;
	int maybedown = 0;

	*host = NULL;
	*test = NULL;
	*log = NULL;
	*color = -1;

	hosttest = hostname = testname = colstr = NULL;
	p = strchr(msg, '\n');
	if (p == NULL) {
		l = strdup(msg);
	}
	else {
		*p = '\0';
		l = strdup(msg); 
		*p = '\n';
	}

	p = strtok(l, " \t");
	if (p) {

		hosttest = strtok(NULL, " \t");
	}
	if (hosttest) colstr = strtok(NULL, " \n\t");

	if (hosttest) {
		hostname = hosttest;
		testname = strrchr(hosttest, '.');
		if (testname) { *testname = '\0'; testname++; }
		p = hostname;
		while ((p = strchr(p, ',')) != NULL) *p = '.';
	}

	hostname = knownhost(hostname, sender, ghosthandling, &maybedown);
	if (hostname == NULL) return;

	for (hwalk = hosts; (hwalk && strcasecmp(hostname, hwalk->hostname)); hwalk = hwalk->next) ;
	if (createhost && (hwalk == NULL)) {
		hwalk = (bbd_hostlist_t *)malloc(sizeof(bbd_hostlist_t));
		hwalk->hostname = strdup(hostname);
		hwalk->logs = NULL;
		hwalk->next = hosts;
		hosts = hwalk;
	}
	for (twalk = tests; (twalk && strcasecmp(testname, twalk->testname)); twalk = twalk->next);
	if (createlog && (twalk == NULL)) {
		twalk = (bbd_testlist_t *)malloc(sizeof(bbd_testlist_t));
		twalk->testname = strdup(testname);
		twalk->next = tests;
		tests = twalk;
	}
	if (hwalk && twalk) {
		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next);
		if (createlog && (lwalk == NULL)) {
			lwalk = (bbd_log_t *)malloc(sizeof(bbd_log_t));
			lwalk->color = lwalk->oldcolor = NO_COLOR;
			lwalk->host = hwalk;
			lwalk->test = twalk;
			lwalk->message = NULL;
			lwalk->msgsz = 0;
			lwalk->dismsg = lwalk->ackmsg = NULL;
			lwalk->lastchange = lwalk->validtime = lwalk->enabletime = lwalk->acktime = 0;
			lwalk->next = hwalk->logs;
			hwalk->logs = lwalk;
		}
	}

	*host = hwalk;
	*test = twalk;
	*log = lwalk;
	if (colstr) {
		*color = parse_color(colstr);
		if ( maybedown && ((*color == COL_RED) || (*color == COL_YELLOW)) ) {
			*color = COL_BLUE;
		}
	}
	free(l);
}

void get_cookiedur(char *msg, char *sender, bbd_log_t **log, int *duration)
{
	unsigned int cookie;
	char durstr[100];
	
	*log = NULL;
	if (sscanf(msg, "%*s %u %99s", &cookie, durstr) == 2) {
		cookie_t *cwalk;

		for (cwalk = cookiehead; (cwalk && (cwalk->cookie != cookie)); cwalk = cwalk->next);

		if (cwalk && (cwalk->expires > time(NULL))) {
			*log = cwalk->log;
			*duration = durationvalue(durstr);
			dprintf("Found cookie for status %s\n", (*log)->message);
		}
	}
}

void handle_status(unsigned char *msg, int msglen, char *sender, char *hostname, char *testname, 
		   bbd_log_t *log, int newcolor)
{
	int validity = 30;	/* validity is counted in minutes */
	int oldcolor;
	time_t now = time(NULL);

	if (strncmp(msg, "status+", 7) == 0) {
		validity = durationvalue(msg+7);
	}

	if (log->enabletime > now) {
		/* The test is currently disabled. */
		newcolor = COL_BLUE;
	}
	else if (log->enabletime) {
		/* A disable has expired. Clear the timestamp and the message buffer */
		log->enabletime = 0;
		if (log->dismsg) { free(log->dismsg); log->dismsg = NULL; }
	}

	if (log->acktime) {
		/* Handling of ack'ed tests */

		if (newcolor == COL_GREEN) {
			/* The test recovered. Clear the ack. */
			log->acktime = 0;
		}

		if (log->acktime > now) {
			/* Dont need to do anything about an acked test */
		}
		else {
			/* The acknowledge has expired. Clear the timestamp and the message buffer */
			log->acktime = 0;
			if (log->ackmsg) { free(log->ackmsg); log->ackmsg = NULL; }
		}
	}

	log->validtime = now + validity*60;
	oldcolor = log->color;
	log->color = newcolor;
	if (msg != log->message) {	/* They can be the same when called from handle_enadis() */
		if ((log->message == NULL) || (log->msgsz = 0)) {
			log->message = strdup(msg);
			log->msgsz = msglen;
		}
		else if (log->msgsz >= msglen) {
			strcpy(log->message, msg);
		}
		else {
			log->message = realloc(log->message, msglen);
			strcpy(log->message, msg);
			log->msgsz = msglen;
		}
	}

	if (oldcolor != newcolor) {
		int oldalertstatus = ((alertcolors & (1 << oldcolor)) != 0);
		int newalertstatus = ((alertcolors & (1 << newcolor)) != 0);

		log->oldcolor = oldcolor;
		dprintf("oldcolor=%d, oldas=%d, newcolor=%d, newas=%d\n", 
			oldcolor, oldalertstatus, newcolor, newalertstatus);

		if (oldalertstatus != newalertstatus) {
			/* alert status changed. Tell the pagers */
			dprintf("posting to page channel\n");
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, (void *) log, NULL);
		}

		dprintf("posting to stachg channel\n");
		posttochannel(stachgchn, channelnames[C_STACHG], msg, sender, hostname, (void *) log, NULL);
		log->lastchange = time(NULL);
	}

	dprintf("posting to status channel\n");
	posttochannel(statuschn, channelnames[C_STATUS], msg, sender, hostname, (void *) log, NULL);
}

void handle_data(char *msg, int msglen, char *sender, char *hostname, char *testname)
{
	posttochannel(datachn, channelnames[C_DATA], msg, sender, hostname, (void *)testname, NULL);
}

void handle_notes(char *msg, int msglen, char *sender, char *hostname)
{
	posttochannel(noteschn, channelnames[C_NOTES], msg, sender, hostname, NULL, NULL);
}

void handle_enadis(int enabled, char *msg, int msglen, char *sender)
{
	char firstline[200];
	char hosttest[200];
	char *tname = NULL;
	char durstr[100];
	int duration = 0;
	int assignments;
	int alltests = 0;
	bbd_hostlist_t *hwalk = NULL;
	bbd_testlist_t *twalk = NULL;
	bbd_log_t *log;
	char *p;
	int maybedown;

	p = strchr(msg, '\n'); 
	if (p == NULL) {
		strncpy(firstline, msg, sizeof(firstline)-1);
	}
	else {
		*p = '\0';
		strncpy(firstline, msg, sizeof(firstline)-1);
		*p = '\n';
	}
	assignments = sscanf(firstline, "%*s %199s %99s", hosttest, durstr);
	if (assignments < 1) return;
	duration = durationvalue(durstr);
	p = hosttest + strlen(hosttest) - 1;
	if (*p == '*') {
		/* It ends with a '*' so assume this is for all tests */
		alltests = 1;
		*p = '\0';
		p--;
		if (*p == '.') *p = '\0';
	}
	else {
		/* No wildcard -> get the test name */
		p = strrchr(hosttest, '.');
		if (p == NULL) return; /* "enable foo" ... surely you must be joking. */
		*p = '\0';
		tname = (p+1);
	}
	p = hosttest; while ((p = strchr(p, ',')) != NULL) *p = '.';


	p = knownhost(hosttest, sender, ghosthandling, &maybedown);
	if (p == NULL) return;
	strcpy(hosttest, p);

	for (hwalk = hosts; (hwalk && strcasecmp(hosttest, hwalk->hostname)); hwalk = hwalk->next) ;
	if (hwalk == NULL) {
		/* Unknown host */
		return;
	}

	if (tname) {
		for (twalk = tests; (twalk && strcasecmp(tname, twalk->testname)); twalk = twalk->next);
		if (twalk == NULL) {
			/* Unknown test */
			return;
		}
	}

	if (enabled) {
		/* Enable is easy - just clear the enabletime */
		if (alltests) {
			for (log = hwalk->logs; (log); log = log->next) {
				log->enabletime = 0;
				if (log->dismsg) {
					free(log->dismsg);
					log->dismsg = NULL;
				}
			}
		}
		else {
			for (log = hwalk->logs; (log && (log->test != twalk)); log = log->next) ;
			if (log) {
				log->enabletime = 0;
				if (log->dismsg) {
					free(log->dismsg);
					log->dismsg = NULL;
				}
			}
		}
	}
	else {
		/* disable code goes here */
		time_t expires = time(NULL) + duration*60;
		char *dismsg;

		p = hosttest; while ((p = strchr(p, '.')) != NULL) *p = ',';

		dismsg = msg;
		while (*dismsg && !isspace(*dismsg)) dismsg++;       /* Skip "disable".... */
		while (*dismsg && isspace(*dismsg)) dismsg++;        /* and the space ... */
		while (*dismsg && !isspace(*dismsg)) dismsg++;       /* and the host.test ... */
		while (*dismsg && isspace(*dismsg)) dismsg++;        /* and the space ... */
		while (*dismsg && !isspace(*dismsg)) dismsg++;       /* and the duration ... */

		if (alltests) {
			for (log = hwalk->logs; (log); log = log->next) {
				log->enabletime = expires;
				if (dismsg) {
					if (log->dismsg) free(log->dismsg);
					log->dismsg = strdup(dismsg);
				}
			}
		}
		else {
			for (log = hwalk->logs; (log && (log->test != twalk)); log = log->next) ;
			if (log) {
				log->enabletime = expires;
				if (dismsg) {
					if (log->dismsg) free(log->dismsg);
					log->dismsg = strdup(dismsg);
				}
			}
		}

		/* Trigger an immediate status update */
		handle_status(log->message, strlen(log->message), sender, 
			      log->host->hostname, log->test->testname, log, COL_BLUE);
	}

	return;
}


void handle_ack(char *msg, char *sender, bbd_log_t *log, int duration)
{
	char *p;

	log->acktime = time(NULL)+duration*60;
	p = msg;
	p += strcspn(p, " \t");		/* Skip the keyword ... */
	p += strspn(p, " \t");		/* and the space ... */
	p += strspn(p, "0123456789");	/* and the cookie ... */
	p += strspn(p, " \t");		/* and the space ... */
	p += strspn(p, "0123456789");	/* and the duration ... */
	p += strspn(p, " \t");		/* and the space ... */
	log->ackmsg = strdup(p);

	/* Tell the pagers */
	posttochannel(pagechn, "ack", log->ackmsg, sender, log->host->hostname, (void *)log, NULL);
	return;
}


void handle_dropnrename(enum droprencmd_t cmd, char *sender, char *hostname, char *n1, char *n2)
{
	int maybedown;
	bbd_hostlist_t *hwalk;
	bbd_testlist_t *twalk, *newt;
	bbd_log_t *lwalk;
	char msgbuf[MAXMSG];
	struct timeval tstamp;
	struct timezone tz;

	msgbuf[0] = '\0';
	hostname = knownhost(hostname, sender, ghosthandling, &maybedown);
	if (hostname == NULL) return;

	for (hwalk = hosts; (hwalk && strcasecmp(hostname, hwalk->hostname)); hwalk = hwalk->next) ;
	if (hwalk == NULL) return;

	gettimeofday(&tstamp, &tz);
	switch (cmd) {
	  case CMD_DROPTEST:
		for (twalk = tests; (twalk && strcasecmp(n1, twalk->testname)); twalk = twalk->next) ;
		if (twalk == NULL) return;

		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next) ;
		if (lwalk == NULL) return;
		if (lwalk == hwalk->logs) {
			hwalk->logs = hwalk->logs->next;
		}
		else {
			bbd_log_t *plog;
			for (plog = hwalk->logs; (plog->next != lwalk); plog = plog->next) ;
			plog->next = lwalk->next;
		}
		if (lwalk->message) free(lwalk->message);
		if (lwalk->dismsg) free(lwalk->dismsg);
		if (lwalk->ackmsg) free(lwalk->ackmsg);
		free(lwalk);
		sprintf(msgbuf, "@@droptest|%d.%06d|%s|%s|%s\n@@\n", 
			(int)tstamp.tv_sec, (int)tstamp.tv_usec, sender, hostname, n1);
		break;

	  case CMD_DROPHOST:
		/* Unlink the hostlist entry */
		if (hwalk == hosts) {
			hosts = hosts->next;
		}
		else {
			bbd_hostlist_t *phost;

			for (phost = hosts; (phost->next != hwalk); phost = phost->next) ;
			phost->next = hwalk->next;
		}

		/* Loop through the host logs and free them */
		lwalk = hwalk->logs;
		while (lwalk) {
			bbd_log_t *tmp = lwalk;
			lwalk = lwalk->next;

			if (tmp->message) free(tmp->message);
			if (tmp->dismsg) free(tmp->dismsg);
			if (tmp->ackmsg) free(tmp->ackmsg);
			free(tmp);
		}

		/* Free the hostlist entry */
		free(hwalk);
		sprintf(msgbuf, "@@drophost|%d.%06d|%s|%s\n@@\n", 
			(int)tstamp.tv_sec, (int)tstamp.tv_usec, sender, hostname);
		break;

	  case CMD_RENAMEHOST:
		if (strlen(hwalk->hostname) <= strlen(n1)) {
			strcpy(hwalk->hostname, n1);
		}
		else {
			free(hwalk->hostname);
			hwalk->hostname = strdup(n1);
		}
		sprintf(msgbuf, "@@renamehost|%d.%06d|%s|%s|%s\n@@\n", 
			(int)tstamp.tv_sec, (int)tstamp.tv_usec, sender, hostname, n1);
		break;

	  case CMD_RENAMETEST:
		for (twalk = tests; (twalk && strcasecmp(n1, twalk->testname)); twalk = twalk->next) ;
		if (twalk == NULL) return;
		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next) ;
		if (lwalk == NULL) return;
		for (newt = tests; (newt && strcasecmp(n2, newt->testname)); newt = newt->next) ;
		if (newt == NULL) {
			newt = (bbd_testlist_t *) malloc(sizeof(bbd_testlist_t));
			newt->testname = strdup(n2);
			newt->next = tests;
			tests = newt;
		}
		lwalk->test = newt;
		sprintf(msgbuf, "@@renametest|%d.%06d|%s|%s|%s|%s\n@@\n", 
			(int)tstamp.tv_sec, (int)tstamp.tv_usec, sender, hostname, n1, n2);
		break;
	}

	if (strlen(msgbuf)) {
		/* Tell the workers */
		posttochannel(statuschn, NULL, NULL, NULL, NULL, NULL, msgbuf);
		posttochannel(stachgchn, NULL, NULL, NULL, NULL, NULL, msgbuf);
		posttochannel(pagechn, NULL, NULL, NULL, NULL, NULL, msgbuf);
		posttochannel(datachn, NULL, NULL, NULL, NULL, NULL, msgbuf);
		posttochannel(noteschn, NULL, NULL, NULL, NULL, NULL, msgbuf);
	}
}


int get_config(char *fn, conn_t *msg)
{
	FILE *fd = NULL;
	int done = 0;
	int n;

	fd = stackfopen(fn, "r");
	if (fd == NULL) return -1;

	*msg->buf = '\0';
	msg->bufp = msg->buf;
	msg->buflen = 0;
	do {
		if ((msg->bufsz - msg->buflen) < 1024) {
			msg->bufsz += 4096;
			msg->buf = realloc(msg->buf, msg->bufsz);
			msg->bufp = msg->buf + msg->buflen;
		}
		done = (stackfgets(msg->bufp, (msg->bufsz - msg->buflen), "include", NULL) == NULL);
		if (!done) {
			n = strlen(msg->bufp);
			msg->buflen += n;
			msg->bufp += n;
		}
	} while (!done);

	stackfclose(fd);
	return 0;
}

void do_message(conn_t *msg)
{
	bbd_hostlist_t *h;
	bbd_testlist_t *t;
	bbd_log_t *log;
	int color;
	char sender[20];

	/* Most likely, we will not send a response */
	msg->doingwhat = NOTALK;
	strcpy(sender, inet_ntoa(msg->addr.sin_addr));

	if (strncmp(msg->buf, "combo\n", 6) == 0) {
		char *currmsg, *nextmsg;

		currmsg = msg->buf+6;
		do {
			nextmsg = strstr(currmsg, "\n\nstatus");
			if (nextmsg) { *(nextmsg+1) = '\0'; nextmsg += 2; }

			get_hts(currmsg, sender, &h, &t, &log, &color, 1, 1);
			if (log && (color != -1)) {
				handle_status(currmsg, strlen(currmsg), sender, h->hostname, t->testname, log, color);
			}

			currmsg = nextmsg;
		} while (currmsg);
	}
	else if (strncmp(msg->buf, "status", 6) == 0) {
		get_hts(msg->buf, sender, &h, &t, &log, &color, 1, 1);
		if (log && (color != -1)) {
			handle_status(msg->buf, msg->buflen, sender, h->hostname, t->testname, log, color);
		}
	}
	else if (strncmp(msg->buf, "data", 4) == 0) {
		get_hts(msg->buf, sender, &h, &t, &log, &color, 1, 1);
		if (h && t) handle_data(msg->buf, msg->buflen, sender, h->hostname, t->testname);
	}
	else if (strncmp(msg->buf, "summary", 7) == 0) {
		get_hts(msg->buf, sender, &h, &t, &log, &color, 1, 1);
		if (log && (color != -1)) {
			handle_status(msg->buf, msg->buflen, sender, h->hostname, t->testname, log, color);
		}
	}
	else if (strncmp(msg->buf, "notes", 5) == 0) {
		char tok[MAXMSG];
		char *hostname;
		int maybedown;
		if (sscanf(msg->buf, "notes %s\n", tok) == 1) {
			hostname  = knownhost(tok, sender, ghosthandling, &maybedown);
			if (hostname) handle_notes(msg->buf, msg->buflen, sender, hostname);
		}
	}
	else if (strncmp(msg->buf, "enable", 6) == 0) {
		handle_enadis(1, msg->buf, msg->buflen, sender);
	}
	else if (strncmp(msg->buf, "disable", 7) == 0) {
		handle_enadis(0, msg->buf, msg->buflen, sender);
	}
	else if (strncmp(msg->buf, "config", 6) == 0) {
		char conffn[1024];

		if ( (sscanf(msg->buf, "config %1023s", conffn) == 1) &&
		     (strstr("../", conffn) == NULL) && (get_config(conffn, msg) == 0) ) {
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf;
		}
	}
	else if (strncmp(msg->buf, "query ", 6) == 0) {
		get_hts(msg->buf, sender, &h, &t, &log, &color, 0, 0);
		if (log) {
			unsigned char *eoln = strchr(log->message, '\n');

			msg->doingwhat = RESPONDING;
			if (eoln) *eoln = '\0';
			msg->bufp = msg->buf = strdup(msg_data(log->message));
			msg->buflen = strlen(msg->buf);
			if (eoln) *eoln = '\n';
		}
	}
	else if (strncmp(msg->buf, "bbgendlog ", 10) == 0) {
		/* 
		 * Request for a single status log
		 * bbgendlog HOST.TEST
		 */
		get_hts(msg->buf, sender, &h, &t, &log, &color, 0, 0);
		if (log) {
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf = strdup(log->message);
			msg->buflen = strlen(msg->buf);
		}
	}
	else if (strcmp(msg->buf, "bbgendboard") == 0) {
		/* Request for a summmary of all known status logs */
		bbd_hostlist_t *hwalk;
		bbd_log_t *lwalk;
		char *buf, *bufp;
		int bufsz, buflen;
		int n;

		bufsz = 102400;
		bufp = buf = (char *)malloc(bufsz);
		buflen = 0;

		for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
			for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
				char *eoln = strchr(lwalk->message, '\n');

				if (eoln) *eoln = '\0';
				if ((bufsz - buflen) < 1024) {
					bufsz += 16384;
					buf = (char *)realloc(buf, bufsz);
					bufp = buf + buflen;
				}
				n = sprintf(bufp, "%s|%s|%s|%d|%d|%s\n", 
					hwalk->hostname, lwalk->test->testname, 
					colnames[lwalk->color],
					(int) lwalk->lastchange, (int) lwalk->validtime,
					msg_data(lwalk->message));
				bufp += n;
				buflen += n;
				if (eoln) *eoln = '\n';
			}
		}

		msg->doingwhat = RESPONDING;
		msg->bufp = msg->buf = buf;
		msg->buflen = buflen;
	}
	else if (strncmp(msg->buf, "bbgendcookie ", 13) == 0) {
		/* bbgendcookie HOST.TEST */
		get_hts(msg->buf, sender, &h, &t, &log, &color, 0, 0);
		if (log) {
			if (cookiehead == NULL) {
				cookiehead = cookietail = (cookie_t *)malloc(sizeof(cookie_t));
			}
			else {
				cookietail->next = (cookie_t *) malloc(sizeof(cookie_t));
				cookietail = cookietail->next;
			}

			cookietail->log = log;
			cookietail->cookie = random();
			cookietail->expires = time(NULL) + 300;
			cookietail->next = NULL;

			msg->doingwhat = RESPONDING;
			msg->buflen = sprintf(msg->buf, "%d\n", cookietail->cookie);
			msg->bufp = msg->buf;
		}
	}
	else if (strncmp(msg->buf, "bbgendack", 9) == 0) {
		/* bbgendack COOKIE DURATION TEXT */
		int duration = 30*60;
		get_cookiedur(msg->buf, sender, &log, &duration);
		if (log) {
			handle_ack(msg->buf, sender, log, duration);
		}
	}
	else if (strncmp(msg->buf, "bbgenddrop ", 11) == 0) {
		char hostname[200];
		char testname[200];
		int n = sscanf(msg->buf, "bbgenddrop %s %s", hostname, testname);

		if (n == 1) {
			handle_dropnrename(CMD_DROPHOST, sender, hostname, NULL, NULL);
		}
		else if (n == 2) {
			handle_dropnrename(CMD_DROPTEST, sender, hostname, testname, NULL);
		}
	}
	else if (strncmp(msg->buf, "bbgendrename ", 13) == 0) {
		char hostname[200];
		char n1[200], n2[200];
		int n = sscanf(msg->buf, "bbgendrename %s %s %s", hostname, n1, n2);

		if (n == 2) {
			/* Host rename */
			handle_dropnrename(CMD_RENAMEHOST, sender, hostname, n1, NULL);
		}
		else if (n == 3) {
			/* Test rename */
			handle_dropnrename(CMD_RENAMETEST, sender, hostname, n1, n2);
		}
	}

	if (msg->doingwhat == RESPONDING) {
		shutdown(msg->sock, SHUT_RD);
	}
	else {
		shutdown(msg->sock, SHUT_RDWR);
		close(msg->sock);
		msg->sock = -1;
	}
}


void save_checkpoint(void)
{
	char *tempfn;
	FILE *fd;
	bbd_hostlist_t *hwalk;
	bbd_log_t *lwalk;
	time_t now = time(NULL);

	if (checkpointfn == NULL) return;

	tempfn = malloc(strlen(checkpointfn) + 20);
	sprintf(tempfn, "%s.%d\n", checkpointfn, (int)now);
	fd = fopen(tempfn, "w");
	if (fd == NULL) {
		errprintf("Cannot open checkpoint file %s\n", tempfn);
		free(tempfn);
		return;
	}

	for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
		for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
			char *statusmsg = base64encode(lwalk->message);
			char *disablemsg = NULL;
			char *ackmsg = NULL;
			
			if (lwalk->dismsg) disablemsg = base64encode(lwalk->dismsg);
			if (lwalk->ackmsg) ackmsg = base64encode(lwalk->ackmsg);

			fprintf(fd, "@@BBGENDCHK-V1|%s|%s|%s|%s|%d|%d|%d|%d|%s|%s|%s\n", 
				hwalk->hostname, lwalk->test->testname, 
				colnames[lwalk->color], colnames[lwalk->oldcolor],
				(int) lwalk->lastchange, (int) lwalk->validtime, 
				(int) lwalk->enabletime, (int) lwalk->acktime,
				statusmsg, (disablemsg ? disablemsg : ""), (ackmsg ? ackmsg : ""));

			if (disablemsg) free(disablemsg);
			if (ackmsg) free(ackmsg);
			free(statusmsg);
		}
	}

	fclose(fd);
	rename(tempfn, checkpointfn);
	free(tempfn);
}


void load_checkpoint(char *fn)
{
	FILE *fd;
	char l[3*MAXMSG];
	char *item;
	int i, err;
	bbd_hostlist_t *htail = NULL;
	bbd_testlist_t *t = NULL;
	bbd_log_t *ltail = NULL;
	char *hostname, *testname, *statusmsg, *disablemsg, *ackmsg;
	time_t lastchange, validtime, enabletime, acktime;
	int color, oldcolor;

	fd = fopen(fn, "r");
	if (fd == NULL) {
		errprintf("Cannot access checkpoint file %s for restore\n", fn);
		return;
	}

	while (fgets(l, sizeof(l)-1, fd)) {
		hostname = testname = statusmsg = disablemsg = NULL;
		lastchange = validtime = enabletime = acktime = 0;
		err =0;

		item = strtok(l, "|\n"); i = 0;
		while (item && !err) {
			switch (i) {
			  case 0: err = (strcmp(item, "@@BBGENDCHK-V1") != 0); break;
			  case 1: hostname = item; break;
			  case 2: testname = item; break;
			  case 3: color = parse_color(item); if (color == -1) err = 1; break;
			  case 4: oldcolor = parse_color(item); if (color == -1) err = 1; break;
			  case 5: lastchange = atoi(item); break;
			  case 6: validtime = atoi(item); break;
			  case 7: enabletime = atoi(item); break;
			  case 8: acktime = atoi(item); break;
			  case 9: statusmsg = item; break;
			  case 10: disablemsg = item; break;
			  case 11: ackmsg = item; break;
			  default: err = 1;
			}

			item = strtok(NULL, "|\n"); i++;
		}

		if (err) continue;

		if ((hosts == NULL) || (strcmp(hostname, htail->hostname) != 0)) {
			/* New host */
			if (hosts == NULL) {
				htail = hosts = (bbd_hostlist_t *) malloc(sizeof(bbd_hostlist_t));
			}
			else {
				htail->next = (bbd_hostlist_t *) malloc(sizeof(bbd_hostlist_t));
				htail = htail->next;
			}
			htail->hostname = strdup(hostname);
			htail->logs = NULL;
			htail->next = NULL;
		}

		for (t=tests; (t && (strcmp(t->testname, testname) != 0)); t = t->next) ;
		if (t == NULL) {
			t = (bbd_testlist_t *) malloc(sizeof(bbd_testlist_t));
			t->testname = strdup(testname);
			t->next = tests;
			tests = t;
		}

		if (htail->logs == NULL) {
			ltail = htail->logs = (bbd_log_t *) malloc(sizeof(bbd_log_t));
		}
		else {
			ltail->next = (bbd_log_t *)malloc(sizeof(bbd_log_t));
			ltail = ltail->next;
		}

		ltail->test = t;
		ltail->host = htail;
		ltail->color = color;
		ltail->lastchange = lastchange;
		ltail->validtime = validtime;
		ltail->enabletime = enabletime;
		ltail->acktime = acktime;
		ltail->message = base64decode(statusmsg);
		ltail->msgsz = strlen(ltail->message);
		ltail->dismsg = ( (disablemsg && strlen(disablemsg)) ? base64decode(disablemsg) : NULL);
		ltail->ackmsg = ( (ackmsg && strlen(ackmsg)) ? base64decode(ackmsg) : NULL);
		ltail->next = NULL;
	}

	fclose(fd);
}


void sig_handler(int signum)
{
	int status;

	switch (signum) {
	  case SIGTERM:
	  case SIGINT:
		running = 0;
		break;

	  case SIGHUP:
		reloadconfig = 1;
		break;

	  case SIGUSR1:
		nextcheckpoint = 0;
		break;

	  case SIGCHLD:
		/* A child exited. Pick up status so we dont leave zombies around */
		wait(&status);
		break;
	}
}


int main(int argc, char *argv[])
{
	conn_t *connhead = NULL, *conntail=NULL;
	char *listenip = "0.0.0.0";
	int listenport = 1984;
	char *bbhostsfn = NULL;
	int checkpointinterval = 300;
	struct sockaddr_in laddr;
	int lsocket, opt;
	int listenq = 512;
	int argi;
	struct timeval tv;
	struct timezone tz;

	colnames[COL_GREEN] = "green";
	colnames[COL_YELLOW] = "yellow";
	colnames[COL_RED] = "red";
	colnames[COL_CLEAR] = "clear";
	colnames[COL_BLUE] = "blue";
	colnames[COL_PURPLE] = "purple";
	colnames[NO_COLOR] = "none";
	gettimeofday(&tv, &tz);
	srandom(tv.tv_usec);

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--listen=", 9) == 0) {
			char *p = strchr(argv[argi], '=') + 1;

			listenip = strdup(p);
			p = strchr(listenip, ':');
			if (p) {
				*p = '\0';
				listenport = atoi(p+1);
			}
		}
		else if (strncmp(argv[argi], "--bbhosts=", 10) == 0) {
			char *p = strchr(argv[argi], '=') + 1;
			bbhostsfn = strdup(p);
		}
		else if (strncmp(argv[argi], "--checkpoint-file=", 18) == 0) {
			char *p = strchr(argv[argi], '=') + 1;
			checkpointfn = strdup(p);
		}
		else if (strncmp(argv[argi], "--checkpoint-interval=", 22) == 0) {
			char *p = strchr(argv[argi], '=') + 1;
			checkpointinterval = atoi(p);
		}
		else if (strncmp(argv[argi], "--restart=", 10) == 0) {
			char *p = strchr(argv[argi], '=') + 1;
			load_checkpoint(p);
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
		else if (strncmp(argv[argi], "--ghosts=", 9) == 0) {
			char *p = strchr(argv[argi], '=') + 1;

			if (strcmp(p, "allow") == 0) ghosthandling = 0;
			else if (strcmp(p, "drop") == 0) ghosthandling = 1;
			else if (strcmp(p, "log") == 0) ghosthandling = 2;
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			printf("Options:\n");
			printf("\t--listen=IP:PORT              : The address the daemon listens on\n");
			printf("\t--bbhosts=FILENAME            : The bb-hosts file\n");
			printf("\t--ghosts=allow|drop|log       : How to handle unknown hosts\n");
			printf("\t--alertcolors=COLOR[,COLOR]   : What colors trigger an alert\n");
			return 1;
		}
	}

	if (getenv("BBHOSTS") && (bbhostsfn == NULL)) {
		bbhostsfn = strdup(getenv("BBHOSTS"));
	}

	if (ghosthandling == -1) {
		if (getenv("BBGHOSTS")) ghosthandling = atoi(getenv("BBGHOSTS"));
		else ghosthandling = 0;
	}

	if (ghosthandling && (bbhostsfn == NULL)) {
		errprintf("No bb-hosts file specified, required when using ghosthandling\n");
		exit(1);
	}
	nextcheckpoint = time(NULL) + checkpointinterval;

	/* Set up a socket to listen for new connections */
	memset(&laddr, 0, sizeof(laddr));
	inet_aton(listenip, (struct in_addr *) &laddr.sin_addr.s_addr);
	laddr.sin_port = htons(listenport);
	laddr.sin_family = AF_INET;
	lsocket = socket(AF_INET, SOCK_STREAM, 0);
	if (lsocket == -1) {
		errprintf("Cannot create listen socket (%s)\n", strerror(errno));
		return 1;
	}
	opt = 1;
	setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	fcntl(lsocket, F_SETFL, O_NONBLOCK);
	if (bind(lsocket, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
		errprintf("Cannot bind to listen socket (%s)\n", strerror(errno));
		return 1;
	}
	if (listen(lsocket, listenq) == -1) {
		errprintf("Cannot listen (%s)\n", strerror(errno));
		return 1;
	}

	setup_signalhandler("bbd_net");
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGCHLD, sig_handler);

	statuschn = setup_channel(C_STATUS, (IPC_CREAT|0600));
	stachgchn = setup_channel(C_STACHG, (IPC_CREAT|0600));
	pagechn   = setup_channel(C_PAGE, (IPC_CREAT|0600));
	datachn   = setup_channel(C_DATA, (IPC_CREAT|0600));
	noteschn  = setup_channel(C_NOTES, (IPC_CREAT|0600));

	do {
		fd_set fdread, fdwrite;
		int maxfd, n;
		conn_t *cwalk;
		time_t now = time(NULL);

		if (reloadconfig && bbhostsfn) {
			reloadconfig = 0;
			load_hostnames(bbhostsfn, 1);
		}

		if (now > nextcheckpoint) {
			pid_t childpid;

			nextcheckpoint = now + checkpointinterval;
			childpid = fork();
			if (childpid == -1) {
				errprintf("Could not fork checkpoing child:%s\n", strerror(errno));
			}
			else if (childpid == 0) {
				save_checkpoint();
				exit(0);
			}
		}

		FD_ZERO(&fdread); FD_ZERO(&fdwrite);
		FD_SET(lsocket, &fdread); maxfd = lsocket;

		for (cwalk = connhead; (cwalk); cwalk = cwalk->next) {
			switch (cwalk->doingwhat) {
				case RECEIVING:
					FD_SET(cwalk->sock, &fdread);
					if (cwalk->sock > maxfd) maxfd = cwalk->sock;
					break;
				case RESPONDING:
					FD_SET(cwalk->sock, &fdwrite);
					if (cwalk->sock > maxfd) maxfd = cwalk->sock;
					break;
			}
		}

		n = select(maxfd+1, &fdread, &fdwrite, NULL, NULL);
		if (n <= 0) {
			if (errno == EINTR) 
				continue;
			else {
				errprintf("Fatal error in select: %s\n", strerror(errno));
				break;
			}
		}

		for (cwalk = connhead; (cwalk); cwalk = cwalk->next) {
			switch (cwalk->doingwhat) {
			  case RECEIVING:
				if (FD_ISSET(cwalk->sock, &fdread)) {
					n = read(cwalk->sock, cwalk->bufp, (cwalk->bufsz - cwalk->buflen - 1));
					if (n <= 0) {
						if (cwalk->buflen) do_message(cwalk);
					}
					else {
						cwalk->bufp += n;
						cwalk->buflen += n;
						*(cwalk->bufp) = '\0';
						if ((cwalk->bufsz - cwalk->buflen) < 2048) {
							cwalk->bufsz += 2048;
							cwalk->buf = (unsigned char *) realloc(cwalk->buf, cwalk->bufsz);
							cwalk->bufp = cwalk->buf + cwalk->buflen;
						}
					}
				}
				break;

			  case RESPONDING:
				if (FD_ISSET(cwalk->sock, &fdwrite)) {
					n = write(cwalk->sock, cwalk->bufp, cwalk->buflen);

					if (n < 0) {
						cwalk->buflen = 0;
					}
					else {
						cwalk->bufp += n;
						cwalk->buflen -= n;
					}

					if (cwalk->buflen == 0) {
						shutdown(cwalk->sock, SHUT_WR);
						close(cwalk->sock); 
						cwalk->sock = -1; 
						cwalk->doingwhat = NOTALK;
					}
				}
				break;
			}
		}

		/* Clean up conn structs that are no longer used */
		while (connhead && (connhead->doingwhat == NOTALK)) {
			conn_t *tmp = connhead;
			connhead = connhead->next;
			free(tmp->buf);
			free(tmp);
		}
		if (connhead == NULL) conntail = NULL;

		/* Clean up cookies that have expired */
		while (cookiehead && (cookiehead->expires <= now)) {
			cookie_t *tmp = cookiehead;
			cookiehead = cookiehead->next;
			free(tmp);
		}
		if (cookiehead == NULL) cookietail = NULL;

		/* Pick up new connections */
		if (FD_ISSET(lsocket, &fdread)) {
			struct sockaddr_in addr;
			int addrsz = sizeof(addr);
			int sock = accept(lsocket, (struct sockaddr *)&addr, &addrsz);

			if (sock >= 0) {
				if (connhead == NULL) {
					connhead = conntail = (conn_t *)malloc(sizeof(conn_t));
				}
				else {
					conntail->next = (conn_t *)malloc(sizeof(conn_t));
					conntail = conntail->next;
				}

				conntail->sock = sock;
				memcpy(&conntail->addr, &addr, sizeof(conntail->addr));
				conntail->doingwhat = RECEIVING;
				conntail->buf = (unsigned char *)malloc(MAXMSG);
				conntail->bufp = conntail->buf;
				conntail->buflen = 0;
				conntail->bufsz = MAXMSG;
				conntail->next = NULL;
			}
		}
	} while (running);

	close_channel(statuschn, 1);
	close_channel(stachgchn, 1);
	close_channel(pagechn, 1);
	close_channel(datachn, 1);
	close_channel(noteschn, 1);
	save_checkpoint();

	return 0;
}

