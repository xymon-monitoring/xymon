/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This is the master daemon, bbgend.                                         */
/*                                                                            */
/* This is a daemon that implements the Big Brother network protocol, with    */
/* additional protocol items implemented for bbgen.                           */
/*                                                                            */
/* This daemon maintains the full state of the BB system in memory, elimi-    */
/* nating the need for file-based storage of e.g. status logs. The web        */
/* frontend programs (bbgen, bbcombotest, bb-hostsvc.cgi etc) can retrieve    */
/* current statuslogs from this daemon to build the BB webpages. However,     */
/* a "plugin" mechanism is also implemented to allow "worker modules" to      */
/* pickup various types of events that occur in the BB system. This allows    */
/* such modules to e.g. maintain the standard BB file-based storage, or       */
/* implement history logging or RRD database updates. This plugin mechanism   */
/* uses System V IPC mechanisms for a high-performance/low-latency communi-   */
/* cation between bbgend and the worker modules - under no circumstances      */
/* should the daemon be tasked with storing data to a low-bandwidth channel.  */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd.c,v 1.61 2004-11-19 22:13:27 henrik Exp $";

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
#include <time.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include "libbbgen.h"

#include "bbgend_ipc.h"
#include "loadhosts.h"

/* This holds the names of the tests we have seen reports for */
typedef struct bbgend_testlist_t {
	char *testname;
	struct bbgend_testlist_t *next;
} bbgend_testlist_t;

/* This holds all information about a single status */
typedef struct bbgend_log_t {
	struct bbgend_hostlist_t *host;
	struct bbgend_testlist_t *test;
	int color, oldcolor;
	char *testflags;
	char sender[16];
	time_t lastchange;	/* time when the currently logged status began */
	time_t logtime;		/* time when last update was received */
	time_t validtime;	/* time when status is no longer valid */
	time_t enabletime;	/* time when test auto-enables after a disable */
	time_t acktime;		/* time when test acknowledgement expires */
	unsigned char *message;
	int msgsz;
	unsigned char *dismsg, *ackmsg;
	int cookie;
	time_t cookieexpires;
	struct bbgend_log_t *next;
} bbgend_log_t;

/* This is a list of the hosts we have seen reports for, and links to their status logs */
typedef struct bbgend_hostlist_t {
	char *hostname;
	char ip[16];
	bbgend_log_t *logs;
	struct bbgend_hostlist_t *next;
} bbgend_hostlist_t;

bbgend_hostlist_t *hosts = NULL;		/* The hosts we have reports from */
bbgend_testlist_t *tests = NULL;		/* The tests we have seen */

typedef struct sender_t {
	unsigned long int ipval;
	int ipmask;
} sender_t;

sender_t *maintsenders = NULL;
sender_t *statussenders = NULL;
sender_t *adminsenders = NULL;
sender_t *wwwsenders = NULL;

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

enum droprencmd_t { CMD_DROPHOST, CMD_DROPTEST, CMD_RENAMEHOST, CMD_RENAMETEST };

static volatile int running = 1;
static volatile int reloadconfig = 1;
static volatile time_t nextcheckpoint = 0;
static volatile int dologswitch = 0;

/* Our channels to worker modules */
bbgend_channel_t *statuschn = NULL;	/* Receives full "status" messages */
bbgend_channel_t *stachgchn = NULL;	/* Receives brief message about a status change */
bbgend_channel_t *pagechn   = NULL;	/* Receives alert messages (triggered from status changes) */
bbgend_channel_t *datachn   = NULL;	/* Receives raw "data" messages */
bbgend_channel_t *noteschn  = NULL;	/* Receives raw "notes" messages */
bbgend_channel_t *enadischn = NULL;	/* Receives "enable" and "disable" messages */

#define NO_COLOR (COL_COUNT)
static char *colnames[COL_COUNT+1];
int alertcolors = ( (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE) );
int ghosthandling = -1;
char *checkpointfn = NULL;
char *purpleclientconn = NULL;

FILE *dbgfd = NULL;
char *dbghost = NULL;

sender_t *getsenderlist(char *iplist)
{
	char *p, *tok;
	sender_t *result;
	int count;

	count = 0; p = iplist; do { count++; p = strchr(p, ','); if (p) p++; } while (p);
	result = (sender_t *) calloc(1, sizeof(sender_t) * (count+1));

	tok = strtok(iplist, ","); count = 0;
	while (tok) {
		int bits = 32;

		p = strchr(tok, '/');
		if (p) *p = '\0';
		result[count].ipval = ntohl(inet_addr(tok));
		if (p) { *p = '/'; p++; bits = atoi(p); }
		if (bits < 32) 
			result[count].ipmask = (0xFFFFFFFF << (32 - atoi(p)));
		else
			result[count].ipmask = 0xFFFFFFFF;

		tok = strtok(NULL, ",");
		count++;
	}

	return result;
}

int oksender(sender_t *oklist, uint32_t sender)
{
	int i;

	if (oklist == NULL) return 1;
	i = 0;
	do {
		if ((oklist[i].ipval & oklist[i].ipmask) == (ntohl(sender) & oklist[i].ipmask)) return 1;
		i++;
	} while (oklist[i].ipval != 0);

	return 0;
}


void posttochannel(bbgend_channel_t *channel, char *channelmarker, 
		   char *msg, char *sender, char *hostname, void *arg, char *readymsg)
{
	bbgend_log_t *log;
	char *testname;
	struct sembuf s;
	struct shmid_ds chninfo;
	int clients;
	int n;
	struct timeval tstamp;
	struct timezone tz;
	int semerr = 0;

	/* First see how many users are on this channel */
	clients = semctl(channel->semid, CLIENTCOUNT, GETVAL);
	if (clients == 0) {
		dprintf("Dropping message - no readers\n");
		return;
	}

	/* 
	 * Wait for BOARDBUSY to go low.
	 * We need a loop here, because if we catch a signal
	 * while waiting on the semaphore, then we need to
	 * re-start the semaphore wait. Otherwise we may
	 * end up with semaphores that are out of sync
	 * (GOCLIENT goes up while a worker waits for it 
	 *  to go to 0).
	 */
	do {
		s.sem_num = BOARDBUSY; s.sem_op = 0; s.sem_flg = 0;
		n = semop(channel->semid, &s, 1);
		if (n == -1) {
			semerr = errno;
			if (semerr != EINTR) errprintf("semop failed, %s\n", strerror(errno));
		}
	} while ((n == -1) && (semerr == EINTR) && running);
	if (!running) return;

	/* All clear, post the message */
	if (channel->seq == 999999) channel->seq = 0;
	channel->seq++;
	gettimeofday(&tstamp, &tz);
	if (readymsg) {
		n = sprintf(channel->channelbuf, 
			    "@@%s#%u|%d.%06d|%s|%s\n@@\n", 
			    channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender,
			    readymsg);
	}
	else {
		switch(channel->channelid) {
		  case C_STATUS:
			log = (bbgend_log_t *)arg;
			n = snprintf(channel->channelbuf, (SHAREDBUFSZ-1),
				"@@%s#%u|%d.%06d|%s|%s|%s|%d|%s|%s|%s|%d", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, hostname, 
				log->test->testname, (int) log->validtime, 
				colnames[log->color], 
				(log->testflags ? log->testflags : ""),
				colnames[log->oldcolor], (int) log->lastchange); 
			n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-1), "|%d|%s",
				(int)log->acktime, nlencode(log->ackmsg));
			n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-1), "|%d|%s",
				(int)log->enabletime, nlencode(log->dismsg));
			n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-1), "\n%s\n@@\n", msg);
			*(channel->channelbuf + n) = '\0';
			break;

		  case C_STACHG:
			log = (bbgend_log_t *)arg;
			n = snprintf(channel->channelbuf, (SHAREDBUFSZ-1),
				"@@%s#%u|%d.%06d|%s|%s|%s|%d|%s|%s|%d\n%s\n@@\n", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, hostname, 
				log->test->testname, (int) log->validtime, 
				colnames[log->color], colnames[log->oldcolor], (int) log->lastchange, 
				msg);
			*(channel->channelbuf + n) = '\0';
			break;

		  case C_PAGE:
			log = (bbgend_log_t *)arg;
			if (strcmp(channelmarker, "ack") == 0) {
				n = snprintf(channel->channelbuf, (SHAREDBUFSZ-1),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d\n%s\n@@\n", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					log->test->testname, log->host->ip,
					(int) log->acktime, msg);
			}
			else {
				n = snprintf(channel->channelbuf, (SHAREDBUFSZ-1),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%d|%s|%d\n%s\n@@\n", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					log->test->testname, log->host->ip, (int) log->validtime, 
					colnames[log->color], colnames[log->oldcolor], (int) log->lastchange,
					hostpagename(hostname), log->cookie, msg);
			}
			*(channel->channelbuf + n) = '\0';
			break;

		  case C_DATA:
			testname = (char *)arg;
			n = snprintf(channel->channelbuf,  (SHAREDBUFSZ-1),
				"@@%s#%u|%d.%06d|%s|%s|%s\n%s\n@@\n", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, hostname, testname, msg);
			*(channel->channelbuf + n) = '\0';
			break;

		  case C_NOTES:
			n = snprintf(channel->channelbuf,  (SHAREDBUFSZ-1),
				"@@%s#%u|%d.%06d|%s|%s\n%s\n@@\n", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, hostname, msg);
			*(channel->channelbuf + n) = '\0';
			break;

		  case C_ENADIS:
			log = (bbgend_log_t *)arg;
			n = snprintf(channel->channelbuf, (SHAREDBUFSZ-1),
				"@@%s#%u|%d.%06d|%s|%s|%s|%d\n@@\n",
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int)tstamp.tv_usec,
				sender, hostname, log->test->testname, (int) log->enabletime);
			*(channel->channelbuf + n) = '\0';
			break;
		}
	}

	/* Let the readers know it is there.  */
	n = shmctl(channel->shmid, IPC_STAT, &chninfo);
	clients = chninfo.shm_nattch-1;		/* Get it again, in case someone attached since last check */
	dprintf("Posting message %u to %d readers\n", channel->seq, clients);
	s.sem_num = BOARDBUSY; s.sem_op = clients; s.sem_flg = 0;		/* Up BOARDBUSY */
	n = semop(channel->semid, &s, 1);

	/* Make sure GOCLIENT is 0 */
	n = semctl(channel->semid, GOCLIENT, GETVAL);
	if (n > 0) {
		errprintf("Oops ... GOCLIENT is high (%d)\n", n);
	}

	s.sem_num = GOCLIENT; s.sem_op = clients; s.sem_flg = 0; 		/* Up GOCLIENT */
	n = semop(channel->semid, &s, 1);
	dprintf("Message posted\n");

	return;
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
		    now = time(NULL);
		    nowtm = localtime(&now);
		    nowtm->tm_mon += result;
		    result = (mktime(nowtm) - now) / 60;
		    break;
	  case 'y': 
		    now = time(NULL);
		    nowtm = localtime(&now);
		    nowtm->tm_year += result;
		    result = (mktime(nowtm) - now) / 60;
		    break;
	}

	return result;
}


void get_hts(char *msg, char *sender, 
	     bbgend_hostlist_t **host, bbgend_testlist_t **test, bbgend_log_t **log, 
	     int *color, int createhost, int createlog)
{
	/*
	 * This routine takes care of finding existing status log records, or
	 * (if they dont exist) creating new ones for an incoming status.
	 *
	 * "msg" contains an incoming message. First list is of the form "KEYWORD host,domain.test COLOR"
	 */

	char *firstline, *p;
	char *hosttest, *hostname, *testname, *colstr;
	char hostip[20];
	bbgend_hostlist_t *hwalk = NULL;
	bbgend_testlist_t *twalk = NULL;
	bbgend_log_t *lwalk = NULL;
	int maybedown = 0;

	*host = NULL;
	*test = NULL;
	*log = NULL;
	*color = -1;

	hosttest = hostname = testname = colstr = NULL;
	p = strchr(msg, '\n');
	if (p == NULL) {
		firstline = strdup(msg);
	}
	else {
		*p = '\0';
		firstline = strdup(msg); 
		*p = '\n';
	}

	p = strtok(firstline, " \t"); /* Keyword ... */
	if (p) hosttest = strtok(NULL, " \t"); /* ... HOST.TEST combo ... */
	if (hosttest == NULL) goto done;
	colstr = strtok(NULL, " \t"); /* ... and the color (if any) */

	if (strncmp(msg, "summary", 7) == 0) {
		/* Summary messages are handled specially */
		hostname = hosttest;	/* This will always be "summary" */
		testname = strchr(hosttest, '.');
		if (testname) { *testname = '\0'; testname++; }
	}
	else {
		hostname = hosttest;
		testname = strrchr(hosttest, '.');
		if (testname) { *testname = '\0'; testname++; }
		p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';

		hostname = knownhost(hostname, hostip, sender, ghosthandling, &maybedown);
		if (hostname == NULL) goto done;
	}

	for (hwalk = hosts; (hwalk && strcasecmp(hostname, hwalk->hostname)); hwalk = hwalk->next) ;
	if (createhost && (hwalk == NULL)) {
		hwalk = (bbgend_hostlist_t *)malloc(sizeof(bbgend_hostlist_t));
		hwalk->hostname = strdup(hostname);
		strcpy(hwalk->ip, hostip);
		hwalk->logs = NULL;
		hwalk->next = hosts;
		hosts = hwalk;
	}
	for (twalk = tests; (twalk && strcasecmp(testname, twalk->testname)); twalk = twalk->next);
	if (createlog && (twalk == NULL)) {
		twalk = (bbgend_testlist_t *)malloc(sizeof(bbgend_testlist_t));
		twalk->testname = strdup(testname);
		twalk->next = tests;
		tests = twalk;
	}
	if (hwalk && twalk) {
		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next);
		if (createlog && (lwalk == NULL)) {
			lwalk = (bbgend_log_t *)malloc(sizeof(bbgend_log_t));
			lwalk->color = lwalk->oldcolor = NO_COLOR;
			lwalk->testflags = NULL;
			lwalk->sender[0] = '\0';
			lwalk->host = hwalk;
			lwalk->test = twalk;
			lwalk->message = NULL;
			lwalk->msgsz = 0;
			lwalk->dismsg = lwalk->ackmsg = NULL;
			lwalk->lastchange = lwalk->validtime = lwalk->enabletime = lwalk->acktime = 0;
			lwalk->cookie = -1;
			lwalk->cookieexpires = 0;
			lwalk->next = hwalk->logs;
			hwalk->logs = lwalk;
		}
	}

done:
	*host = hwalk;
	*test = twalk;
	*log = lwalk;
	if (colstr) {
		*color = parse_color(colstr);
		if ( maybedown && ((*color == COL_RED) || (*color == COL_YELLOW)) ) {
			*color = COL_BLUE;
		}
	}
	free(firstline);
}


bbgend_log_t *find_cookie(int cookie)
{
	/*
	 * Find a cookie we have issued.
	 */
	bbgend_log_t *result = NULL;
	bbgend_hostlist_t *hwalk = NULL;
	bbgend_log_t *lwalk = NULL;
	int found = 0;

	for (hwalk = hosts; (hwalk && !found); hwalk = hwalk->next) {
		for (lwalk = hwalk->logs; (lwalk && (lwalk->cookie != cookie)); lwalk = lwalk->next);
		found = (lwalk != NULL);
	}

	if (found && lwalk && (lwalk->cookieexpires > time(NULL))) {
		result = lwalk;
	}

	return result;
}


void handle_status(unsigned char *msg, char *sender, char *hostname, char *testname, bbgend_log_t *log, int newcolor)
{
	int validity = 30;	/* validity is counted in minutes */
	time_t now = time(NULL);
	int msglen, issummary, oldalertstatus, newalertstatus;

	if (msg == NULL) {
		errprintf("handle_status got a NULL message for %s.%s, sender %s\n", hostname, testname, sender);
		return;
	}

	msglen = strlen(msg);
	issummary = (strncmp(msg, "summary", 7) == 0);

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
		posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, (void *)log, NULL);
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

	log->logtime = now;
	log->validtime = now + validity*60;
	strncpy(log->sender, sender, sizeof(log->sender)-1);
	log->oldcolor = log->color;
	log->color = newcolor;
	oldalertstatus = ((alertcolors & (1 << log->oldcolor)) != 0);
	newalertstatus = ((alertcolors & (1 << newcolor)) != 0);

	if (msg != log->message) {	/* They can be the same when called from handle_enadis() or check_purple_upd() */
		char *p;

		/*
		 * Note here:
		 * - log->msgsz is the buffer size INCLUDING the final \0.
		 * - msglen is the message length WITHOUT the final \0.
		 */
		if ((log->message == NULL) || (log->msgsz == 0)) {
			/* No buffer - get one */
			log->message = (unsigned char *)malloc(msglen+1);
			memcpy(log->message, msg, msglen+1);
			log->msgsz = msglen+1;
		}
		else if (log->msgsz > msglen) {
			/* Message - including \0 - fits into the existing buffer. */
			memcpy(log->message, msg, msglen+1);
		}
		else {
			/* Message does not fit into existing buffer. Grow it. */
			log->message = (unsigned char *)realloc(log->message, msglen+1);
			memcpy(log->message, msg, msglen+1);
			log->msgsz = msglen+1;
		}

		/* Get at the test flags. They are immediately after the color */
		p = msg_data(msg);
		if (p) {
			p += strlen(colorname(newcolor));
		}
		else {
			p = "";
			errprintf("msg_data returned a NULL\n");
		}
		if (strncmp(p, " <!-- [flags:", 13) == 0) {
			char *flagstart = p+13;
			char *flagend = strchr(flagstart, ']');

			if (flagend) {
				*flagend = '\0';
				if (log->testflags == NULL)
					log->testflags = strdup(flagstart);
				else if (strlen(log->testflags) >= strlen(flagstart))
					strcpy(log->testflags, flagstart);
				else {
					log->testflags = realloc(log->testflags, strlen(flagstart));
					strcpy(log->testflags, flagstart);
				}
				*flagend = ']';
			}
		}
	}

	/* If in an alert state, we may need to generate a cookie */
	if (newalertstatus) {
		if (log->cookieexpires < now) {
			int newcookie;

			/* Need to ensure that cookies are unique, hence the loop */
			log->cookie = -1; log->cookieexpires = 0;
			do {
				newcookie = (random() % 1000000);
			} while (find_cookie(newcookie));

			log->cookie = newcookie;
			log->cookieexpires = log->validtime;
		}
	}
	else {
		log->cookie = -1;
		log->cookieexpires = 0;
	}

	if ((log->oldcolor != newcolor) && !issummary) {
		dprintf("oldcolor=%d, oldas=%d, newcolor=%d, newas=%d\n", 
			log->oldcolor, oldalertstatus, newcolor, newalertstatus);

		/*
		 * We pass the message to the page channel, IF
		 * - the alert status changes, OR
		 * - it is in an alert status, and the color changes.
		 */
		if ((oldalertstatus != newalertstatus) || (newalertstatus && (log->oldcolor != newcolor))) {
			dprintf("posting to page channel\n");
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, (void *) log, NULL);
		}

		/*
		 * Change of color always goes to the status-change channel.
		 */
		dprintf("posting to stachg channel\n");
		posttochannel(stachgchn, channelnames[C_STACHG], msg, sender, hostname, (void *) log, NULL);
		log->lastchange = time(NULL);
	}

	dprintf("posting to status channel\n");
	posttochannel(statuschn, channelnames[C_STATUS], msg, sender, hostname, (void *) log, NULL);
	return;
}

void handle_data(char *msg, char *sender, char *hostname, char *testname)
{
	posttochannel(datachn, channelnames[C_DATA], msg, sender, hostname, (void *)testname, NULL);
}

void handle_notes(char *msg, char *sender, char *hostname)
{
	posttochannel(noteschn, channelnames[C_NOTES], msg, sender, hostname, NULL, NULL);
}

void handle_enadis(int enabled, char *msg, char *sender)
{
	char firstline[MAXMSG];
	char hosttest[200];
	char hostip[20];
	char *tname = NULL;
	char durstr[100];
	int duration = 0;
	int assignments;
	int alltests = 0;
	bbgend_hostlist_t *hwalk = NULL;
	bbgend_testlist_t *twalk = NULL;
	bbgend_log_t *log;
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


	p = knownhost(hosttest, hostip, sender, ghosthandling, &maybedown);
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
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, (void *)log, NULL);
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
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, (void *)log, NULL);
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
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, (void *)log, NULL);
				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->testname, log, COL_BLUE);
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
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, (void *)log, NULL);

				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->testname, log, COL_BLUE);
			}
		}

	}

	return;
}


void handle_ack(char *msg, char *sender, bbgend_log_t *log, int duration)
{
	char *p;

	log->acktime = time(NULL)+duration*60;
	p = msg;
	p += strspn(p, " \t");			/* Skip the space ... */
	p += strspn(p, "-0123456789");		/* and the cookie ... */
	p += strspn(p, " \t");			/* and the space ... */
	p += strspn(p, "0123456789hdwmy");	/* and the duration ... */
	p += strspn(p, " \t");			/* and the space ... */
	log->ackmsg = strdup(p);

	/* Tell the pagers */
	posttochannel(pagechn, "ack", log->ackmsg, sender, log->host->hostname, (void *)log, NULL);
	return;
}


void free_log_t(bbgend_log_t *zombie)
{
	if (zombie->message) free(zombie->message);
	if (zombie->dismsg) free(zombie->dismsg);
	if (zombie->ackmsg) free(zombie->ackmsg);
	free(zombie);
}

void handle_dropnrename(enum droprencmd_t cmd, char *sender, char *hostname, char *n1, char *n2)
{
	int maybedown;
	char hostip[20];
	bbgend_hostlist_t *hwalk;
	bbgend_testlist_t *twalk, *newt;
	bbgend_log_t *lwalk;
	char msgbuf[MAXMSG];
	char *marker = NULL;

	/*
	 * We pass drop- and rename-messages to the workers, whether 
	 * we know about this host or not. It could be that the drop command
	 * arrived after we had already re-loaded the bb-hosts file, and 
	 * so the host is no longer known by us - but there is still some
	 * data stored about it that needs to be cleaned up.
	 */
	msgbuf[0] = '\0';
	switch (cmd) {
	  case CMD_DROPTEST:
		marker = "droptest";
		sprintf(msgbuf, "%s|%s", hostname, n1);
		break;
	  case CMD_DROPHOST:
		marker = "drophost";
		sprintf(msgbuf, "%s", hostname);
		break;
	  case CMD_RENAMEHOST:
		marker = "renamehost";
		sprintf(msgbuf, "%s|%s", hostname, n1);
		break;
	  case CMD_RENAMETEST:
		marker = "renametest";
		sprintf(msgbuf, "%s|%s|%s", hostname, n1, n2);
		break;
	}

	if (strlen(msgbuf)) {
		/* Tell the workers */
		posttochannel(statuschn, marker, NULL, sender, NULL, NULL, msgbuf);
		posttochannel(stachgchn, marker, NULL, sender, NULL, NULL, msgbuf);
		posttochannel(pagechn, marker, NULL, sender, NULL, NULL, msgbuf);
		posttochannel(datachn, marker, NULL, sender, NULL, NULL, msgbuf);
		posttochannel(noteschn, marker, NULL, sender, NULL, NULL, msgbuf);
		posttochannel(enadischn, marker, NULL, sender, NULL, NULL, msgbuf);
	}


	/*
	 * Now clean up our internal state info, if there is any.
	 */
	hostname = knownhost(hostname, hostip, sender, ghosthandling, &maybedown);
	if (hostname == NULL) return;

	for (hwalk = hosts; (hwalk && strcasecmp(hostname, hwalk->hostname)); hwalk = hwalk->next) ;
	if (hwalk == NULL) return;

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
			bbgend_log_t *plog;
			for (plog = hwalk->logs; (plog->next != lwalk); plog = plog->next) ;
			plog->next = lwalk->next;
		}
		free_log_t(lwalk);
		break;

	  case CMD_DROPHOST:
		/* Unlink the hostlist entry */
		if (hwalk == hosts) {
			hosts = hosts->next;
		}
		else {
			bbgend_hostlist_t *phost;

			for (phost = hosts; (phost->next != hwalk); phost = phost->next) ;
			phost->next = hwalk->next;
		}

		/* Loop through the host logs and free them */
		lwalk = hwalk->logs;
		while (lwalk) {
			bbgend_log_t *tmp = lwalk;
			lwalk = lwalk->next;

			free_log_t(tmp);
		}

		/* Free the hostlist entry */
		free(hwalk);
		break;

	  case CMD_RENAMEHOST:
		if (strlen(hwalk->hostname) <= strlen(n1)) {
			strcpy(hwalk->hostname, n1);
		}
		else {
			free(hwalk->hostname);
			hwalk->hostname = strdup(n1);
		}
		break;

	  case CMD_RENAMETEST:
		for (twalk = tests; (twalk && strcasecmp(n1, twalk->testname)); twalk = twalk->next) ;
		if (twalk == NULL) return;
		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next) ;
		if (lwalk == NULL) return;
		for (newt = tests; (newt && strcasecmp(n2, newt->testname)); newt = newt->next) ;
		if (newt == NULL) {
			newt = (bbgend_testlist_t *) malloc(sizeof(bbgend_testlist_t));
			newt->testname = strdup(n2);
			newt->next = tests;
			tests = newt;
		}
		lwalk->test = newt;
		break;
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
	bbgend_hostlist_t *h;
	bbgend_testlist_t *t;
	bbgend_log_t *log;
	int color;
	char sender[20];
	time_t now;
	char *msgfrom;

	/* Most likely, we will not send a response */
	msg->doingwhat = NOTALK;
	strcpy(sender, inet_ntoa(msg->addr.sin_addr));
	now = time(NULL);

	if (strncmp(msg->buf, "combo\n", 6) == 0) {
		char *currmsg, *nextmsg;

		if (!oksender(statussenders, msg->addr.sin_addr.s_addr)) goto done;

		currmsg = msg->buf+6;
		do {
			nextmsg = strstr(currmsg, "\n\nstatus");
			if (nextmsg) { *(nextmsg+1) = '\0'; nextmsg += 2; }

			get_hts(currmsg, sender, &h, &t, &log, &color, 1, 1);
			if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
				fprintf(dbgfd, "\n---- combo message from %s ----\n%s---- end message ----\n", sender, currmsg);
				fflush(dbgfd);
			}
			if (log && (color != -1)) {
				msgfrom = strstr(currmsg, "\nStatus message received from ");
				if (msgfrom) {
					sscanf(msgfrom, "\nStatus message received from %s\n", sender);
					*msgfrom = '\0';
				}

				handle_status(currmsg, sender, h->hostname, t->testname, log, color);
			}

			currmsg = nextmsg;
		} while (currmsg);
	}
	else if (strncmp(msg->buf, "status", 6) == 0) {
		if (!oksender(statussenders, msg->addr.sin_addr.s_addr)) goto done;

		get_hts(msg->buf, sender, &h, &t, &log, &color, 1, 1);
		if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
			fprintf(dbgfd, "\n---- status message from %s ----\n%s---- end message ----\n", sender, msg->buf);
			fflush(dbgfd);
		}
		if (log && (color != -1)) {
			msgfrom = strstr(msg->buf, "\nStatus message received from ");
			if (msgfrom) {
				sscanf(msgfrom, "\nStatus message received from %s\n", sender);
				*msgfrom = '\0';
			}

			handle_status(msg->buf, sender, h->hostname, t->testname, log, color);
		}
	}
	else if (strncmp(msg->buf, "data", 4) == 0) {
		char tok[MAXMSG];
		char *hostname = NULL, *testname = NULL;
		int maybedown;
		char hostip[20];

		if (!oksender(statussenders, msg->addr.sin_addr.s_addr)) goto done;

		if (sscanf(msg->buf, "data %s\n", tok) == 1) {
			if ((testname = strrchr(tok, '.')) != NULL) {
				char *p;
				*testname = '\0'; 
				testname++; 
				p = tok; while ((p = strchr(p, ',')) != NULL) *p = '.';
				hostname = knownhost(tok, hostip, sender, ghosthandling, &maybedown);
			}
			if (hostname && testname) handle_data(msg->buf, sender, hostname, testname);
		}
	}
	else if (strncmp(msg->buf, "summary", 7) == 0) {
		if (!oksender(statussenders, msg->addr.sin_addr.s_addr)) goto done;

		get_hts(msg->buf, sender, &h, &t, &log, &color, 1, 1);
		if (log && (color != -1)) {
			handle_status(msg->buf, sender, h->hostname, t->testname, log, color);
		}
	}
	else if (strncmp(msg->buf, "notes", 5) == 0) {
		char tok[MAXMSG];
		char *hostname;
		int maybedown;
		char hostip[20];

		if (!oksender(maintsenders, msg->addr.sin_addr.s_addr)) goto done;

		if (sscanf(msg->buf, "notes %s\n", tok) == 1) {
			char *p;

			p = tok; while ((p = strchr(p, ',')) != NULL) *p = '.';
			hostname = knownhost(tok, hostip, sender, ghosthandling, &maybedown);
			if (hostname) handle_notes(msg->buf, sender, hostname);
		}
	}
	else if (strncmp(msg->buf, "enable", 6) == 0) {
		if (!oksender(maintsenders, msg->addr.sin_addr.s_addr)) goto done;
		handle_enadis(1, msg->buf, sender);
	}
	else if (strncmp(msg->buf, "disable", 7) == 0) {
		if (!oksender(maintsenders, msg->addr.sin_addr.s_addr)) goto done;
		handle_enadis(0, msg->buf, sender);
	}
	else if (strncmp(msg->buf, "config", 6) == 0) {
		char conffn[1024];

		if (!oksender(adminsenders, msg->addr.sin_addr.s_addr)) goto done;

		if ( (sscanf(msg->buf, "config %1023s", conffn) == 1) &&
		     (strstr("../", conffn) == NULL) && (get_config(conffn, msg) == 0) ) {
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf;
		}
	}
	else if (strncmp(msg->buf, "query ", 6) == 0) {
		if (!oksender(adminsenders, msg->addr.sin_addr.s_addr)) goto done;

		get_hts(msg->buf, sender, &h, &t, &log, &color, 0, 0);
		if (log) {
			msg->doingwhat = RESPONDING;
			if (log->message) {
				unsigned char *eoln;

				eoln = strchr(log->message, '\n');
				if (eoln) *eoln = '\0';
				strcpy(msg->buf, msg_data(log->message));
				msg->bufp = msg->buf;
				msg->buflen = strlen(msg->buf);
				if (eoln) *eoln = '\n';
			}
			else {
				msg->buf = msg->bufp = strdup("");
				msg->buflen = 0;
			}
		}
	}
	else if (strncmp(msg->buf, "bbgendlog ", 10) == 0) {
		/* 
		 * Request for a single status log
		 * bbgendlog HOST.TEST
		 *
		 * hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|ackmsg|dismsg
		 */
		if (!oksender(wwwsenders, msg->addr.sin_addr.s_addr)) goto done;

		get_hts(msg->buf, sender, &h, &t, &log, &color, 0, 0);
		if (log) {
			char *buf, *bufp;
			int bufsz, buflen;
			char *ackmsg = "", *dismsg = "";

			if (log->message == NULL) {
				errprintf("%s.%s has a NULL message\n", log->host->hostname, log->test->testname);
				log->message = strdup("");
			}

			bufsz = 1024 + strlen(log->message);
			if (log->ackmsg) bufsz += 2*strlen(log->ackmsg);
			if (log->dismsg) bufsz += 2*strlen(log->dismsg);

			free(msg->buf);
			bufp = buf = (char *)malloc(bufsz);
			buflen = 0;

			bufp += sprintf(bufp, "%s|%s|%s|%s|%d|%d|%d|%d|%d|%s|%d", 
					h->hostname, log->test->testname, 
					colnames[log->color], 
					(log->testflags ? log->testflags : ""),
					(int) log->lastchange, (int) log->logtime, (int) log->validtime, 
					(int) log->acktime, (int) log->enabletime,
					log->sender, log->cookie);

			if (log->ackmsg && (log->acktime > now)) ackmsg = nlencode(log->ackmsg);
			bufp += sprintf(bufp, "|%s", ackmsg);

			if (log->dismsg && (log->enabletime > now)) dismsg = nlencode(log->dismsg);
			bufp += sprintf(bufp, "|%s", dismsg);

			bufp += sprintf(bufp, "\n%s", msg_data(log->message));

			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf = buf;
			msg->buflen = (bufp - buf);
		}
	}
	else if (strncmp(msg->buf, "bbgendboard", 11) == 0) {
		/* 
		 * Request for a summmary of all known status logs
		 *
		 * hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|1st line of message
		 */
		bbgend_hostlist_t *hwalk;
		bbgend_log_t *lwalk;
		char *buf, *bufp;
		int bufsz, buflen;
		int n;

		if (!oksender(wwwsenders, msg->addr.sin_addr.s_addr)) goto done;

		bufsz = 16384;
		bufp = buf = (char *)malloc(bufsz);
		buflen = 0;

		for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
			for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
				char *eoln;
				
				if (lwalk->message == NULL) {
					errprintf("%s.%s has a NULL message\n", lwalk->host->hostname, lwalk->test->testname);
					lwalk->message = strdup("");
				}

				eoln = strchr(lwalk->message, '\n');
				if (eoln) *eoln = '\0';
				if ((bufsz - buflen - strlen(lwalk->message)) < 1024) {
					bufsz += 16384;
					buf = (char *)realloc(buf, bufsz);
					bufp = buf + buflen;
				}
				n = sprintf(bufp, "%s|%s|%s|%s|%d|%d|%d|%d|%d|%s|%d|%s\n", 
					hwalk->hostname, lwalk->test->testname, 
					colnames[lwalk->color], 
					(lwalk->testflags ? lwalk->testflags : ""),
					(int) lwalk->lastchange, 
					(int) lwalk->logtime, (int) lwalk->validtime,
					(int) lwalk->acktime, (int) lwalk->enabletime,
					lwalk->sender, lwalk->cookie, msg_data(lwalk->message));
				bufp += n;
				buflen += n;
				if (eoln) *eoln = '\n';
			}
		}

		free(msg->buf);
		msg->doingwhat = RESPONDING;
		msg->bufp = msg->buf = buf;
		msg->buflen = buflen;
	}
	else if (strncmp(msg->buf, "bbgendlist", 10) == 0) {
		/* 
		 * Request for a list of all known status logs.
		 *
		 * hostname|testname
		 */
		bbgend_hostlist_t *hwalk;
		bbgend_log_t *lwalk;
		char *buf, *bufp;
		int bufsz, buflen;
		int n;

		if (!oksender(wwwsenders, msg->addr.sin_addr.s_addr)) goto done;

		bufsz = 16384;
		bufp = buf = (char *)malloc(bufsz);
		buflen = 0;

		for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
			for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
				if ((bufsz - buflen - strlen(lwalk->message)) < 1024) {
					bufsz += 16384;
					buf = (char *)realloc(buf, bufsz);
					bufp = buf + buflen;
				}

				n = sprintf(bufp, "%s|%s\n", hwalk->hostname, lwalk->test->testname);
				bufp += n;
				buflen += n;
			}
		}

		free(msg->buf);
		msg->doingwhat = RESPONDING;
		msg->bufp = msg->buf = buf;
		msg->buflen = buflen;
	}
	else if ((strncmp(msg->buf, "bbgendack", 9) == 0) || (strncmp(msg->buf, "ack ack_event", 13) == 0)) {
		/* bbgendack COOKIE DURATION TEXT */
		char *p;
		int cookie, duration;
		char durstr[100];
		bbgend_log_t *lwalk;

		if (!oksender(maintsenders, msg->addr.sin_addr.s_addr)) goto done;

		/*
		 * For just a bit of compatibility with the old BB system,
		 * we will accept an "ack ack_event" message. This allows us
		 * to work with existing acknowledgement scripts.
		 */
		if (strncmp(msg->buf, "bbgendack", 9) == 0) p = msg->buf + 9;
		else if (strncmp(msg->buf, "ack ack_event", 13) == 0) p = msg->buf + 13;
		else p = msg->buf;

		if (sscanf(p, "%d %99s", &cookie, durstr) == 2) {
			log = find_cookie(abs(cookie));
			if (log) {
				duration = durationvalue(durstr);
				if (cookie > 0)
					handle_ack(p, sender, log, duration);
				else {
					/*
					 * Negative cookies mean to ack all pending alerts for
					 * the host. So loop over the host logs and ack all that
					 * have a valid cookie (i.e. not -1)
					 */
					for (lwalk = log->host->logs; (lwalk); lwalk = lwalk->next) {
						if (lwalk->cookie != -1) handle_ack(p, sender, lwalk, duration);
					}
				}
			}
		}
	}
	else if (strncmp(msg->buf, "bbgenddrop ", 11) == 0) {
		char hostname[200];
		char testname[200];
		int n;

		if (!oksender(adminsenders, msg->addr.sin_addr.s_addr)) goto done;

		n = sscanf(msg->buf, "bbgenddrop %199s %199s", hostname, testname);
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
		int n;

		if (!oksender(adminsenders, msg->addr.sin_addr.s_addr)) goto done;

		n = sscanf(msg->buf, "bbgendrename %199s %199s %199s", hostname, n1, n2);
		if (n == 2) {
			/* Host rename */
			handle_dropnrename(CMD_RENAMEHOST, sender, hostname, n1, NULL);
		}
		else if (n == 3) {
			/* Test rename */
			handle_dropnrename(CMD_RENAMETEST, sender, hostname, n1, n2);
		}
	}

done:
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
	bbgend_hostlist_t *hwalk;
	bbgend_log_t *lwalk;
	time_t now = time(NULL);

	if (checkpointfn == NULL) return;

	tempfn = malloc(strlen(checkpointfn) + 20);
	sprintf(tempfn, "%s.%d", checkpointfn, (int)now);
	fd = fopen(tempfn, "w");
	if (fd == NULL) {
		errprintf("Cannot open checkpoint file %s\n", tempfn);
		free(tempfn);
		return;
	}

	for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
		for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
			if (lwalk->dismsg && (lwalk->enabletime < now)) {
				free(lwalk->dismsg);
				lwalk->dismsg = NULL;
				lwalk->enabletime = 0;
			}
			if (lwalk->ackmsg && (lwalk->acktime < now)) {
				free(lwalk->ackmsg);
				lwalk->ackmsg = NULL;
				lwalk->acktime = 0;
			}
			fprintf(fd, "@@BBGENDCHK-V1|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%s", 
				hwalk->hostname, lwalk->test->testname, lwalk->sender,
				colnames[lwalk->color], 
				(lwalk->testflags ? lwalk->testflags : ""),
				colnames[lwalk->oldcolor],
				(int)lwalk->logtime, (int) lwalk->lastchange, (int) lwalk->validtime, 
				(int) lwalk->enabletime, (int) lwalk->acktime, 
				lwalk->cookie, (int) lwalk->cookieexpires,
				nlencode(lwalk->message));
			fprintf(fd, "|%s", nlencode(lwalk->dismsg));
			fprintf(fd, "|%s", nlencode(lwalk->ackmsg));
			fprintf(fd, "\n");
		}
	}

	fclose(fd);
	rename(tempfn, checkpointfn);
	free(tempfn);
}


void load_checkpoint(char *fn)
{
	FILE *fd;
	char l[4*MAXMSG];
	char *item;
	int i, err, maybedown;
	char hostip[20];
	bbgend_hostlist_t *htail = NULL;
	bbgend_testlist_t *t = NULL;
	bbgend_log_t *ltail = NULL;
	char *hostname = NULL, *testname = NULL, *sender = NULL, *testflags = NULL; 
	char *statusmsg = NULL, *disablemsg = NULL, *ackmsg = NULL;
	time_t logtime = 0, lastchange = 0, validtime = 0, enabletime = 0, acktime = 0, cookieexpires = 0;
	int color = COL_GREEN, oldcolor = COL_GREEN, cookie = -1;

	fd = fopen(fn, "r");
	if (fd == NULL) {
		errprintf("Cannot access checkpoint file %s for restore\n", fn);
		return;
	}

	while (fgets(l, sizeof(l)-1, fd)) {
		hostname = testname = sender = testflags = statusmsg = disablemsg = ackmsg = NULL;
		lastchange = validtime = enabletime = acktime = 0;
		err =0;

		item = gettok(l, "|\n"); i = 0;
		while (item && !err) {
			switch (i) {
			  case 0: err = (strcmp(item, "@@BBGENDCHK-V1") != 0); break;
			  case 1: if (strlen(item)) hostname = item; else err=1; break;
			  case 2: if (strlen(item)) testname = item; else err=1; break;
			  case 3: sender = item; break;
			  case 4: color = parse_color(item); if (color == -1) err = 1; break;
			  case 5: testflags = item; break;
			  case 6: oldcolor = parse_color(item); if (oldcolor == -1) oldcolor = NO_COLOR; break;
			  case 7: logtime = atoi(item); break;
			  case 8: lastchange = atoi(item); break;
			  case 9: validtime = atoi(item); break;
			  case 10: enabletime = atoi(item); break;
			  case 11: acktime = atoi(item); break;
			  case 12: cookie = atoi(item); break;
			  case 13: cookieexpires = atoi(item); break;
			  case 14: if (strlen(item)) statusmsg = item; else err=1; break;
			  case 15: disablemsg = item; break;
			  case 16: ackmsg = item; break;
			  default: err = 1;
			}

			item = gettok(NULL, "|\n"); i++;
		}

		if (i < 16) {
			errprintf("Too few fields in record - found %d, expected 16\n", i);
			err = 1;
		}

		if (err) continue;

		/* Only load hosts we know; they may have been dropped while we were offline */
		hostname = knownhost(hostname, hostip, "bbgendchk", ghosthandling, &maybedown);
		if (hostname == NULL) continue;

		if ((hosts == NULL) || (strcmp(hostname, htail->hostname) != 0)) {
			/* New host */
			if (hosts == NULL) {
				htail = hosts = (bbgend_hostlist_t *) malloc(sizeof(bbgend_hostlist_t));
			}
			else {
				htail->next = (bbgend_hostlist_t *) malloc(sizeof(bbgend_hostlist_t));
				htail = htail->next;
			}
			htail->hostname = strdup(hostname);
			strcpy(htail->ip, hostip);
			htail->logs = NULL;
			htail->next = NULL;
		}

		for (t=tests; (t && (strcmp(t->testname, testname) != 0)); t = t->next) ;
		if (t == NULL) {
			t = (bbgend_testlist_t *) malloc(sizeof(bbgend_testlist_t));
			t->testname = strdup(testname);
			t->next = tests;
			tests = t;
		}

		if (htail->logs == NULL) {
			ltail = htail->logs = (bbgend_log_t *) malloc(sizeof(bbgend_log_t));
		}
		else {
			ltail->next = (bbgend_log_t *)malloc(sizeof(bbgend_log_t));
			ltail = ltail->next;
		}

		ltail->test = t;
		ltail->host = htail;
		ltail->color = color;
		ltail->oldcolor = oldcolor;
		ltail->testflags = ( (testflags && strlen(testflags)) ? strdup(testflags) : NULL);
		strcpy(ltail->sender, sender);
		ltail->logtime = logtime;
		ltail->lastchange = lastchange;
		ltail->validtime = validtime;
		ltail->enabletime = enabletime;
		ltail->acktime = acktime;
		nldecode(statusmsg);
		ltail->message = strdup(statusmsg);
		ltail->msgsz = strlen(statusmsg)+1;
		if (disablemsg && strlen(disablemsg)) {
			nldecode(disablemsg);
			ltail->dismsg = strdup(disablemsg);
		}
		else 
			ltail->dismsg = NULL;
		if (ackmsg && strlen(ackmsg)) {
			nldecode(ackmsg);
			ltail->ackmsg = strdup(ackmsg);
		}
		else 
			ltail->ackmsg = NULL;
		ltail->cookie = cookie;
		ltail->cookieexpires = cookieexpires;
		ltail->next = NULL;
	}

	fclose(fd);
}


void check_purple_status(void)
{
	bbgend_hostlist_t *hwalk;
	bbgend_log_t *lwalk;
	time_t now = time(NULL);

	for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
		lwalk = hwalk->logs;
		while (lwalk) {
			if (lwalk->validtime < now) {
				if (strcmp(hwalk->hostname, "summary") == 0) {
					/*
					 * A summary has gone stale. Drop it.
					 */
					bbgend_log_t *tmp;

					if (lwalk == hwalk->logs) {
						tmp = hwalk->logs;
						hwalk->logs = lwalk->next;
						lwalk = lwalk->next;
					}
					else {
						for (tmp = hwalk->logs; (tmp->next != lwalk); tmp = tmp->next);
						tmp->next = lwalk->next;
						tmp = lwalk;
						lwalk = lwalk->next;
					}
					free_log_t(tmp);
				}
				else {
					bbgend_log_t *tmp;
					int newcolor = COL_PURPLE;

					if (purpleclientconn) {
						/*
						 * See if this is a (client) test where we have a red "conn" test.
						 */
						for (tmp = hwalk->logs; (tmp && strcmp(tmp->test->testname, purpleclientconn)); tmp = tmp->next) ;
						if (tmp && (tmp->color == COL_RED)) newcolor = COL_CLEAR;
					}

					handle_status(lwalk->message, "bbgend", 
						hwalk->hostname, lwalk->test->testname, lwalk, newcolor);
					lwalk = lwalk->next;
				}
			}
			else {
				lwalk = lwalk->next;
			}
		}
	}
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
		dologswitch = 1;
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
	char *restartfn = NULL;
	char *logfn = NULL;
	int checkpointinterval = 900;
	int do_purples = 1;
	time_t nextpurpleupdate;
	struct sockaddr_in laddr;
	int lsocket, opt;
	int listenq = 512;
	int argi;
	struct timeval tv;
	struct timezone tz;
	int daemonize = 0;
	char *pidfile = NULL;
	struct sigaction sa;

	colnames[COL_GREEN] = "green";
	colnames[COL_YELLOW] = "yellow";
	colnames[COL_RED] = "red";
	colnames[COL_CLEAR] = "clear";
	colnames[COL_BLUE] = "blue";
	colnames[COL_PURPLE] = "purple";
	colnames[NO_COLOR] = "none";
	gettimeofday(&tv, &tz);
	srandom(tv.tv_usec);

	/* Dont save the error buffer */
	save_errbuf = 0;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--debug")) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--listen=")) {
			char *p = strchr(argv[argi], '=') + 1;

			listenip = strdup(p);
			p = strchr(listenip, ':');
			if (p) {
				*p = '\0';
				listenport = atoi(p+1);
			}
		}
		else if (argnmatch(argv[argi], "--bbhosts=")) {
			char *p = strchr(argv[argi], '=') + 1;
			bbhostsfn = strdup(p);
		}
		else if (argnmatch(argv[argi], "--checkpoint-file=")) {
			char *p = strchr(argv[argi], '=') + 1;
			checkpointfn = strdup(p);
		}
		else if (argnmatch(argv[argi], "--checkpoint-interval=")) {
			char *p = strchr(argv[argi], '=') + 1;
			checkpointinterval = atoi(p);
		}
		else if (argnmatch(argv[argi], "--restart=")) {
			char *p = strchr(argv[argi], '=') + 1;
			restartfn = strdup(p);
		}
		else if (argnmatch(argv[argi], "--alertcolors=")) {
			char *colspec = strchr(argv[argi], '=') + 1;
			int c, ac;
			char *p;
			int colormask;

			p = strtok(colspec, ",");
			ac = 0;
			while (p) {
				c = parse_color(p);
				if (c != -1) ac = (ac | (1 << c));
				p = strtok(NULL, ",");
			}

			/* green and blue can NEVER be alertcolors */
			colormask = ~((1 << COL_GREEN) | (1 << COL_BLUE));
			alertcolors = (ac & colormask);
		}
		else if (argnmatch(argv[argi], "--ghosts=")) {
			char *p = strchr(argv[argi], '=') + 1;

			if (strcmp(p, "allow") == 0) ghosthandling = 0;
			else if (strcmp(p, "drop") == 0) ghosthandling = 1;
			else if (strcmp(p, "log") == 0) ghosthandling = 2;
		}
		else if (argnmatch(argv[argi], "--no-purple")) {
			do_purples = 0;
		}
		else if (argnmatch(argv[argi], "--purple-conn=")) {
			char *p = strchr(argv[argi], '=');
			purpleclientconn = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--daemon")) {
			daemonize = 1;
		}
		else if (argnmatch(argv[argi], "--no-daemon")) {
			daemonize = 0;
		}
		else if (argnmatch(argv[argi], "--pidfile=")) {
			char *p = strchr(argv[argi], '=');
			pidfile = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--log=")) {
			char *p = strchr(argv[argi], '=');
			logfn = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--maint-senders=")) {
			/* Who is allowed to send us "enable", "disable", "ack", "notes" messages */
			char *p = strchr(argv[argi], '=');
			maintsenders = getsenderlist(p+1);
		}
		else if (argnmatch(argv[argi], "--status-senders=")) {
			/* Who is allowed to send us "status", "combo", "summary", "data" messages */
			char *p = strchr(argv[argi], '=');
			statussenders = getsenderlist(p+1);
		}
		else if (argnmatch(argv[argi], "--admin-senders=")) {
			/* Who is allowed to send us "bbgenddrop", "bbgendrename", "config", "query" messages */
			char *p = strchr(argv[argi], '=');
			adminsenders = getsenderlist(p+1);
		}
		else if (argnmatch(argv[argi], "--www-senders=")) {
			/* Who is allowed to send us "bbgendboard", "bbgendlog", "bbgendlist" messages */
			char *p = strchr(argv[argi], '=');
			wwwsenders = getsenderlist(p+1);
		}
		else if (argnmatch(argv[argi], "--dbghost=")) {
			char *p = strchr(argv[argi], '=');

			dbghost = strdup(p+1);
			dbgfd = fopen("/tmp/bbgend.dbg", "a");
		}
		else if (argnmatch(argv[argi], "--help")) {
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

	if (restartfn) {
		load_hostnames(bbhostsfn, get_fqdn());
		load_checkpoint(restartfn);
	}

	nextcheckpoint = time(NULL) + checkpointinterval;
	nextpurpleupdate = time(NULL) + 600;	/* Wait 10 minutes the first time */

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

	/* Go daemon */
	if (daemonize) {
		pid_t childpid;

		/* Become a daemon */
		childpid = fork();
		if (childpid < 0) {
			/* Fork failed */
			errprintf("Could not fork\n");
			exit(1);
		}
		else if (childpid > 0) {
			/* Parent - save PID and exit */
			FILE *fd = fopen(pidfile, "w");
			if (fd) {
				fprintf(fd, "%d\n", (int)childpid);
				fclose(fd);
			}
			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	setup_signalhandler("bbgend");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	statuschn = setup_channel(C_STATUS, CHAN_MASTER);
	if (statuschn == NULL) { errprintf("Cannot setup status channel\n"); return 1; }
	stachgchn = setup_channel(C_STACHG, CHAN_MASTER);
	if (stachgchn == NULL) { errprintf("Cannot setup stachg channel\n"); return 1; }
	pagechn   = setup_channel(C_PAGE, CHAN_MASTER);
	if (pagechn == NULL) { errprintf("Cannot setup page channel\n"); return 1; }
	datachn   = setup_channel(C_DATA, CHAN_MASTER);
	if (datachn == NULL) { errprintf("Cannot setup data channel\n"); return 1; }
	noteschn  = setup_channel(C_NOTES, CHAN_MASTER);
	if (noteschn == NULL) { errprintf("Cannot setup notes channel\n"); return 1; }
	enadischn  = setup_channel(C_ENADIS, CHAN_MASTER);
	if (enadischn == NULL) { errprintf("Cannot setup enadis channel\n"); return 1; }

	freopen("/dev/null", "r", stdin);
	if (logfn) {
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	do {
		fd_set fdread, fdwrite;
		int maxfd, n;
		conn_t *cwalk;
		time_t now = time(NULL);

		if (logfn && dologswitch) {
			freopen(logfn, "a", stdout);
			freopen(logfn, "a", stderr);
			dologswitch = 0;
		}

		if (reloadconfig && bbhostsfn) {
			reloadconfig = 0;
			load_hostnames(bbhostsfn, get_fqdn());
		}

		if (now > nextcheckpoint) {
			pid_t childpid;

			reloadconfig = 1;
			nextcheckpoint = now + checkpointinterval;
			childpid = fork();
			if (childpid == -1) {
				errprintf("Could not fork checkpoint child:%s\n", strerror(errno));
			}
			else if (childpid == 0) {
				save_checkpoint();
				exit(0);
			}
		}

		if (do_purples && (now > nextpurpleupdate)) {
			nextpurpleupdate = time(NULL) + 60;
			check_purple_status();
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
						if (cwalk->buflen) {
							do_message(cwalk);
						}
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
		{
			conn_t *tmp, *khead;

			khead = NULL; cwalk = connhead;
			while (cwalk) {
				if ((cwalk == connhead) && (cwalk->doingwhat == NOTALK)) {
					/* head of chain is dead */
					tmp = connhead;
					connhead = connhead->next;
					tmp->next = khead;
					khead = tmp;

					cwalk = connhead;
				}
				else if (cwalk->next && (cwalk->next->doingwhat == NOTALK)) {
					tmp = cwalk->next;
					cwalk->next = tmp->next;
					tmp->next = khead;
					khead = tmp;

					/* cwalk is unchanged */
				}
				else {
					cwalk = cwalk->next;
				}
			}
			if (connhead == NULL) {
				conntail = NULL;
			}
			else {
				conntail = connhead;
				cwalk = connhead->next;
				if (cwalk) {
					while (cwalk->next) cwalk = cwalk->next;
					conntail = cwalk;
				}
			}

			while (khead) {
				tmp = khead;
				khead = khead->next;

				if (tmp->buf) free(tmp->buf);
				free(tmp);
			}
		}

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
				conntail->bufsz = MAXMSG+2048;
				conntail->buf = (unsigned char *)malloc(conntail->bufsz);
				conntail->bufp = conntail->buf;
				conntail->buflen = 0;
				conntail->next = NULL;
			}
		}
	} while (running);

	/* Tell the workers we to shutdown also */
	running = 1;   /* Kludge, but it's the only way to get posttochannel to do something. */
	posttochannel(statuschn, "shutdown", NULL, "bbgend", NULL, NULL, "");
	posttochannel(stachgchn, "shutdown", NULL, "bbgend", NULL, NULL, "");
	posttochannel(pagechn, "shutdown", NULL, "bbgend", NULL, NULL, "");
	posttochannel(datachn, "shutdown", NULL, "bbgend", NULL, NULL, "");
	posttochannel(noteschn, "shutdown", NULL, "bbgend", NULL, NULL, "");
	posttochannel(enadischn, "shutdown", NULL, "bbgend", NULL, NULL, "");
	running = 0;

	/* Close the channels */
	close_channel(statuschn, CHAN_MASTER);
	close_channel(stachgchn, CHAN_MASTER);
	close_channel(pagechn, CHAN_MASTER);
	close_channel(datachn, CHAN_MASTER);
	close_channel(noteschn, CHAN_MASTER);
	close_channel(enadischn, CHAN_MASTER);

	save_checkpoint();
	unlink(pidfile);

	if (dbgfd) fclose(dbgfd);

	return 0;
}

