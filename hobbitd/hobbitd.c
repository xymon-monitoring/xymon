/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is the master daemon, hobbitd.                                        */
/*                                                                            */
/* This is a daemon that implements the Big Brother network protocol, with    */
/* additional protocol items implemented for Hobbit.                          */
/*                                                                            */
/* This daemon maintains the full state of the Hobbit system in memory,       */
/* eliminating the need for file-based storage of e.g. status logs. The web   */
/* frontend programs (bbgen, bbcombotest, bb-hostsvc.cgi etc) can retrieve    */
/* current statuslogs from this daemon to build the Hobbit webpages. However, */
/* a "plugin" mechanism is also implemented to allow "worker modules" to      */
/* pickup various types of events that occur in the system. This allows       */
/* such modules to e.g. maintain the standard Hobbit file-based storage, or   */
/* implement history logging or RRD database updates. This plugin mechanism   */
/* uses System V IPC mechanisms for a high-performance/low-latency communi-   */
/* cation between hobbitd and the worker modules - under no circumstances     */
/* should the daemon be tasked with storing data to a low-bandwidth channel.  */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd.c,v 1.144 2005-05-07 07:00:56 henrik Exp $";

#include <limits.h>
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
#include <sys/shm.h>
#include <sys/wait.h>

#include "libbbgen.h"

#include "hobbitd_ipc.h"

/* This holds the names of the tests we have seen reports for */
typedef struct hobbitd_testlist_t {
	char *testname;
	struct hobbitd_testlist_t *next;
} hobbitd_testlist_t;

htnames_t *metanames = NULL;
typedef struct hobbitd_meta_t {
	htnames_t *metaname;
	char *value;
	struct hobbitd_meta_t *next;
} hobbitd_meta_t;

/* This holds all information about a single status */
typedef struct hobbitd_log_t {
	struct hobbitd_hostlist_t *host;
	struct hobbitd_testlist_t *test;
	struct htnames_t *origin;
	int color, oldcolor, activealert;
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
	struct hobbitd_meta_t *metas;
	struct hobbitd_log_t *next;
} hobbitd_log_t;

/* This is a list of the hosts we have seen reports for, and links to their status logs */
typedef struct hobbitd_hostlist_t {
	char *hostname;
	char ip[16];
	hobbitd_log_t *logs;
	struct hobbitd_hostlist_t *next;
} hobbitd_hostlist_t;

hobbitd_hostlist_t *hosts = NULL;		/* The hosts we have reports from */
hobbitd_testlist_t *tests = NULL;		/* The tests we have seen */
htnames_t *origins = NULL;

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

/* This struct describes an active connection with a Hobbit client */
typedef struct conn_t {
	int sock;			/* Communications socket */
	struct sockaddr_in addr;	/* Client source address */
	unsigned char *buf, *bufp;	/* Message buffer and pointer */
	int buflen, bufsz;		/* Active and maximum length of buffer */
	int doingwhat;			/* Communications state (NOTALK, READING, RESPONDING) */
	time_t timeout;			/* When the timeout for this connection happens */
	struct conn_t *next;
} conn_t;

enum droprencmd_t { CMD_DROPHOST, CMD_DROPTEST, CMD_RENAMEHOST, CMD_RENAMETEST, CMD_DROPSTATE };

static volatile int running = 1;
static volatile int reloadconfig = 1;
static volatile time_t nextcheckpoint = 0;
static volatile int dologswitch = 0;
static volatile int gotalarm = 0;

/* Our channels to worker modules */
hobbitd_channel_t *statuschn = NULL;	/* Receives full "status" messages */
hobbitd_channel_t *stachgchn = NULL;	/* Receives brief message about a status change */
hobbitd_channel_t *pagechn   = NULL;	/* Receives alert messages (triggered from status changes) */
hobbitd_channel_t *datachn   = NULL;	/* Receives raw "data" messages */
hobbitd_channel_t *noteschn  = NULL;	/* Receives raw "notes" messages */
hobbitd_channel_t *enadischn = NULL;	/* Receives "enable" and "disable" messages */

#define NO_COLOR (COL_COUNT)
static char *colnames[COL_COUNT+1];
int alertcolors, okcolors;
enum alertstate_t { A_OK, A_ALERT, A_UNDECIDED };

typedef struct ghostlist_t {
	char *name;
	char *sender;
	struct ghostlist_t *next;
} ghostlist_t;

int ghosthandling = -1;
ghostlist_t *ghostlist = NULL;

char *checkpointfn = NULL;
FILE *dbgfd = NULL;
char *dbghost = NULL;
time_t boottime;
pid_t parentpid = 0;

typedef struct hobbitd_statistics_t {
	char *cmd;
	unsigned long count;
} hobbitd_statistics_t;

hobbitd_statistics_t hobbitd_stats[] = {
	{ "status", 0 },
	{ "combo", 0 },
	{ "page", 0 },
	{ "summary", 0 },
	{ "data", 0 },
	{ "notes", 0 },
	{ "enable", 0 },
	{ "disable", 0 },
	{ "ack", 0 },
	{ "config", 0 },
	{ "query", 0 },
	{ "hobbitdboard", 0 },
	{ "hobbitdlog", 0 },
	{ "drop", 0 },
	{ "rename", 0 },
	{ "dummy", 0 },
	{ "notify", 0 },
	{ "schedule", 0 },
	{ NULL, 0 }
};

enum boardfield_t { F_NONE, F_HOSTNAME, F_TESTNAME, F_COLOR, F_FLAGS, 
		    F_LASTCHANGE, F_LOGTIME, F_VALIDTIME, F_ACKTIME, F_DISABLETIME,
		    F_SENDER, F_COOKIE, F_LINE1,
		    F_ACKMSG, F_DISMSG, F_MSG, 
		    F_LAST };

typedef struct boardfieldnames_t {
	char *name;
	enum boardfield_t id;
} boardfieldnames_t;
boardfieldnames_t boardfieldnames[] = {
	{ "hostname", F_HOSTNAME },
	{ "testname", F_TESTNAME },
	{ "color", F_COLOR },
	{ "flags", F_FLAGS },
	{ "lastchange", F_LASTCHANGE },
	{ "logtime", F_LOGTIME },
	{ "validtime", F_VALIDTIME },
	{ "acktime", F_ACKTIME },
	{ "disabletime", F_DISABLETIME },
	{ "sender", F_SENDER },
	{ "cookie", F_COOKIE },
	{ "line1", F_LINE1 },
	{ "ackmsg", F_ACKMSG },
	{ "dismsg", F_DISMSG },
	{ "msg", F_MSG },
	{ NULL, F_LAST },
};
enum boardfield_t boardfields[F_LAST];

unsigned long msgs_total = 0;
unsigned long msgs_total_last = 0;
time_t last_stats_time = 0;

/* List of scheduled (future) tasks */
typedef struct scheduletask_t {
	int id;
	time_t executiontime;
	char *command;
	char *sender;
	struct scheduletask_t *next;
} scheduletask_t;
scheduletask_t *schedulehead = NULL;
int nextschedid = 1;

void update_statistics(char *cmd)
{
	int i;

	msgs_total++;

	i = 0;
	while (hobbitd_stats[i].cmd && strncmp(hobbitd_stats[i].cmd, cmd, strlen(hobbitd_stats[i].cmd))) { i++; }
	hobbitd_stats[i].count++;
}

char *generate_stats(void)
{
	static char *statsbuf = NULL;
	static int statsbuflen = 0;
	time_t now = time(NULL);
	char *bufp;
	int i, clients;
	char bootuptxt[40];
	char uptimetxt[40];
	time_t uptime = (now - boottime);

	MEMDEFINE(bootuptxt);
	MEMDEFINE(uptimetxt);

	if (statsbuf == NULL) {
		statsbuflen = 8192;
		statsbuf = (char *)malloc(statsbuflen);
	}
	bufp = statsbuf;

	strftime(bootuptxt, sizeof(bootuptxt), "%d-%b-%Y %T", localtime(&boottime));
	sprintf(uptimetxt, "%d days, %02d:%02d:%02d", 
		(int)(uptime / 86400), (int)(uptime % 86400)/3600, (int)(uptime % 3600)/60, (int)(uptime % 60));

	bufp += sprintf(bufp, "status %s.hobbitd %s\nStatistics for Hobbit daemon\nUp since %s (%s)\n\n",
			xgetenv("MACHINE"), colorname(errbuf ? COL_YELLOW : COL_GREEN), bootuptxt, uptimetxt);
	bufp += sprintf(bufp, "Incoming messages      : %10ld\n", msgs_total);
	i = 0;
	while (hobbitd_stats[i].cmd) {
		bufp += sprintf(bufp, "- %-20s : %10ld\n", hobbitd_stats[i].cmd, hobbitd_stats[i].count);
		i++;
	}
	bufp += sprintf(bufp, "- %-20s : %10ld\n", "Bogus/Timeouts ", hobbitd_stats[i].count);

	if ((now > last_stats_time) && (last_stats_time > 0)) {
		bufp += sprintf(bufp, "Incoming messages/sec  : %10ld (average last %d seconds)\n", 
			((msgs_total - msgs_total_last) / (now - last_stats_time)), 
			(int)(now - last_stats_time));
	}
	msgs_total_last = msgs_total;

	bufp += sprintf(bufp, "\n");
	clients = semctl(statuschn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "status channel messages: %10ld (%d readers)\n", statuschn->msgcount, clients);
	clients = semctl(stachgchn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "stachg channel messages: %10ld (%d readers)\n", stachgchn->msgcount, clients);
	clients = semctl(pagechn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "page   channel messages: %10ld (%d readers)\n", pagechn->msgcount, clients);
	clients = semctl(datachn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "data   channel messages: %10ld (%d readers)\n", datachn->msgcount, clients);
	clients = semctl(noteschn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "notes  channel messages: %10ld (%d readers)\n", noteschn->msgcount, clients);
	clients = semctl(enadischn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "enadis channel messages: %10ld (%d readers)\n", enadischn->msgcount, clients);

	if (ghostlist) {
		ghostlist_t *tmp;

		bufp += sprintf(bufp, "\n\nGhost reports:\n");
		while (ghostlist) {
			if ((statsbuflen - (bufp - statsbuf)) < 512) {
				/* Less than 512 bytes left in buffer - expand it */
				statsbuflen += 4096;
				statsbuf = (char *)realloc(statsbuf, statsbuflen);
				bufp = statsbuf + strlen(statsbuf);
			}
			bufp += sprintf(bufp, "  %-15s reported host %s\n", ghostlist->sender, ghostlist->name);
			tmp = ghostlist;
			ghostlist = ghostlist->next;
			xfree(tmp->name); xfree(tmp->sender); xfree(tmp);
		}
	}

	if (errbuf) {
		if ((strlen(statsbuf) + strlen(errbuf) + 1024) > statsbuflen) {
			statsbuflen = strlen(statsbuf) + strlen(errbuf) + 1024;
			statsbuf = (char *)realloc(statsbuf, statsbuflen);
			bufp = statsbuf + strlen(statsbuf);
		}

		bufp += sprintf(bufp, "\n\nLatest errormessages:\n%s\n", errbuf);
	}

	MEMUNDEFINE(bootuptxt);
	MEMUNDEFINE(uptimetxt);

	return statsbuf;
}


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

int oksender(sender_t *oklist, char *targetip, struct in_addr sender, char *msgbuf)
{
	int i;
	unsigned long int tg_ip;
	char *eoln;

	/* If oklist is empty, we're not doing any access checks - so return OK */
	if (oklist == NULL) return 1;

	/* If we know the target, it would be ok for the host to report on itself. */
	if (targetip) {
		if (strcmp(targetip, "0.0.0.0") == 0) return 1; /* DHCP hosts can report from any address */
		tg_ip = ntohl(inet_addr(targetip));
		if (ntohl(sender.s_addr) == tg_ip) return 1;
	}

	/* It's someone else reporting about the host. Check the access list */
	i = 0;
	do {
		if ((oklist[i].ipval & oklist[i].ipmask) == (ntohl(sender.s_addr) & oklist[i].ipmask)) return 1;
		i++;
	} while (oklist[i].ipval != 0);

	/* Refuse and log the message */
	eoln = strchr(msgbuf, '\n'); if (eoln) *eoln = '\0';
	errprintf("Refused message from %s: %s\n", inet_ntoa(sender), msgbuf);
	if (eoln) *eoln = '\n';

	return 0;
}

enum alertstate_t decide_alertstate(int color)
{
	if ((okcolors & (1 << color)) != 0) return A_OK;
	else if ((alertcolors & (1 << color)) != 0) return A_ALERT;
	else return A_UNDECIDED;
}


void posttochannel(hobbitd_channel_t *channel, char *channelmarker, 
		   char *msg, char *sender, char *hostname, hobbitd_log_t *log, char *readymsg)
{
	struct sembuf s;
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
	gotalarm = 0; alarm(5);
	do {
		s.sem_num = BOARDBUSY; s.sem_op = 0; s.sem_flg = 0;
		n = semop(channel->semid, &s, 1);
		if (n == -1) {
			semerr = errno;
			if (semerr != EINTR) errprintf("semop failed, %s\n", strerror(errno));
		}
	} while ((n == -1) && (semerr == EINTR) && running && !gotalarm);
	alarm(0);
	if (!running) return;

	/* Check if the alarm fired */
	if (gotalarm) {
		errprintf("BOARDBUSY locked at %d, GETNCNT is %d, GETPID is %d, %d clients\n",
			  semctl(channel->semid, BOARDBUSY, GETVAL),
			  semctl(channel->semid, BOARDBUSY, GETNCNT),
			  semctl(channel->semid, BOARDBUSY, GETPID),
			  semctl(channel->semid, CLIENTCOUNT, GETVAL));
		return;
	}

	/* All clear, post the message */
	if (channel->seq == 999999) channel->seq = 0;
	channel->seq++;
	channel->msgcount++;
	gettimeofday(&tstamp, &tz);
	if (readymsg) {
		n = snprintf(channel->channelbuf, (SHAREDBUFSZ-5),
			    "@@%s#%u|%d.%06d|%s|%s", 
			    channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender,
			    readymsg);
		if (n > (SHAREDBUFSZ-5)) errprintf("Oversize data msg from %s truncated (n=%d)\n", sender, n);
		*(channel->channelbuf + SHAREDBUFSZ - 5) = '\0';
	}
	else {
		switch(channel->channelid) {
		  case C_STATUS:
			n = snprintf(channel->channelbuf, (SHAREDBUFSZ-5),
				"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%s|%d", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, log->origin->name, hostname, log->test->testname, 
				(int) log->validtime, colnames[log->color], (log->testflags ? log->testflags : ""),
				colnames[log->oldcolor], (int) log->lastchange); 
			if (n < (SHAREDBUFSZ-5)) {
				n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-5), "|%d|%s",
					(int)log->acktime, nlencode(log->ackmsg));
			}
			if (n < (SHAREDBUFSZ-5)) {
				n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-5), "|%d|%s",
					(int)log->enabletime, nlencode(log->dismsg));
			}
			if (n < (SHAREDBUFSZ-5)) {
				n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-5), "\n%s", msg);
			}
			if (n > (SHAREDBUFSZ-5)) {
				errprintf("Oversize status msg from %s:%s truncated (n=%d)\n", 
					hostname, log->test->testname, n);
			}
			*(channel->channelbuf + SHAREDBUFSZ - 5) = '\0';
			break;

		  case C_STACHG:
			n = snprintf(channel->channelbuf, (SHAREDBUFSZ-5),
				"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%d", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, log->origin->name, hostname, log->test->testname, 
				(int) log->validtime, colnames[log->color], 
				colnames[log->oldcolor], (int) log->lastchange);
			if (n < (SHAREDBUFSZ-5)) {
				n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-5), "|%d|%s",
					(int)log->enabletime, nlencode(log->dismsg));
			}
			if (n < (SHAREDBUFSZ-5)) {
				n += snprintf(channel->channelbuf+n, (SHAREDBUFSZ-n-5), "\n%s", msg);
			}
			if (n > (SHAREDBUFSZ-5)) {
				errprintf("Oversize stachg msg from %s:%s truncated (n=%d)\n", 
					hostname, log->test->testname, n);
			}
			*(channel->channelbuf + SHAREDBUFSZ - 5) = '\0';
			break;

		  case C_PAGE:
			if (strcmp(channelmarker, "ack") == 0) {
				n = snprintf(channel->channelbuf, (SHAREDBUFSZ-5),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d\n%s", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					log->test->testname, log->host->ip,
					(int) log->acktime, msg);
			}
			else if (strcmp(channelmarker, "notify") == 0) {
				namelist_t *hi = hostinfo(hostname);

				n = snprintf(channel->channelbuf, (SHAREDBUFSZ-5),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s\n%s", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					(log->test ? log->test->testname : ""), 
					(hi ? hi->page->pagepath : ""), 
					msg);
			}
			else {
				namelist_t *hi = hostinfo(hostname);

				n = snprintf(channel->channelbuf, (SHAREDBUFSZ-5),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%d|%s|%d\n%s", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					log->test->testname, log->host->ip, (int) log->validtime, 
					colnames[log->color], colnames[log->oldcolor], (int) log->lastchange,
					(hi ? hi->page->pagepath : ""), 
					log->cookie, msg);
			}
			if (n > (SHAREDBUFSZ-5)) {
				errprintf("Oversize page/ack/notify msg from %s:%s truncated (n=%d)\n", 
					hostname, (log->test ? log->test->testname : "<none>"), n);
			}
			*(channel->channelbuf + SHAREDBUFSZ - 5) = '\0';
			break;

		  case C_DATA:
			/* Data channel messages are pre-formatted so we never go here */
			break;

		  case C_NOTES:
			n = snprintf(channel->channelbuf,  (SHAREDBUFSZ-5),
				"@@%s#%u|%d.%06d|%s|%s\n%s", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, hostname, msg);
			if (n > (SHAREDBUFSZ-5)) {
				errprintf("Oversize notes msg from %s:%s truncated (n=%d)\n", 
					hostname, log->test->testname, n);
			}
			*(channel->channelbuf + SHAREDBUFSZ - 5) = '\0';
			break;

		  case C_ENADIS:
			n = snprintf(channel->channelbuf, (SHAREDBUFSZ-5),
				"@@%s#%u|%d.%06d|%s|%s|%s|%d",
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int)tstamp.tv_usec,
				sender, hostname, log->test->testname, (int) log->enabletime);
			if (n > (SHAREDBUFSZ-5)) {
				errprintf("Oversize enadis msg from %s:%s truncated (n=%d)\n", 
					hostname, log->test->testname, n);
			}
			*(channel->channelbuf + SHAREDBUFSZ - 5) = '\0';
			break;

		  case C_LAST:
			break;
		}
	}
	/* Terminate the message */
	strncat(channel->channelbuf, "\n@@\n", (SHAREDBUFSZ-1));

	/* Let the readers know it is there.  */
	clients = semctl(channel->semid, CLIENTCOUNT, GETVAL); /* Get it again, maybe changed since last check */
	dprintf("Posting message %u to %d readers\n", channel->seq, clients);
	/* Up BOARDBUSY */
	s.sem_num = BOARDBUSY; 
	s.sem_op = (clients - semctl(channel->semid, BOARDBUSY, GETVAL)); 
	if (s.sem_op <= 0) {
		errprintf("How did this happen? clients=%d, s.sem_op=%d\n", clients, s.sem_op);
		s.sem_op = clients;
	}
	s.sem_flg = 0;
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


void log_ghost(char *hostname, char *sender, char *msg)
{
	ghostlist_t *gwalk;

	/* If debugging, log the full request */
	if (dbgfd) {
		fprintf(dbgfd, "\n---- combo message from %s ----\n%s---- end message ----\n", sender, msg);
		fflush(dbgfd);
	}

	if ((ghosthandling < 2) || (hostname == NULL) || (sender == NULL)) return;

	for (gwalk = ghostlist; (gwalk && (strcmp(gwalk->name, hostname) != 0)); gwalk = gwalk->next) ;
	if (gwalk == NULL) {
		gwalk = (ghostlist_t *)malloc(sizeof(ghostlist_t));
		gwalk->name = strdup(hostname);
		gwalk->sender = strdup(sender);
		gwalk->next = ghostlist;
		ghostlist = gwalk;
	}
}

void get_hts(char *msg, char *sender, char *origin,
	     hobbitd_hostlist_t **host, hobbitd_testlist_t **test, hobbitd_log_t **log, 
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
	hobbitd_hostlist_t *hwalk = NULL;
	hobbitd_testlist_t *twalk = NULL;
	htnames_t *owalk = NULL;
	hobbitd_log_t *lwalk = NULL;
	int maybedown = 0;

	MEMDEFINE(hostip);
	*hostip = '\0';

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
		char *knownname;

		hostname = hosttest;
		testname = strrchr(hosttest, '.');
		if (testname) { *testname = '\0'; testname++; }
		p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';

		knownname = knownhost(hostname, hostip, ghosthandling, &maybedown);
		if (knownname == NULL) {
			log_ghost(hostname, sender, msg);
			goto done;
		}
		hostname = knownname;
	}

	for (hwalk = hosts; (hwalk && strcasecmp(hostname, hwalk->hostname)); hwalk = hwalk->next) ;
	if (createhost && (hwalk == NULL)) {
		hwalk = (hobbitd_hostlist_t *)malloc(sizeof(hobbitd_hostlist_t));
		hwalk->hostname = strdup(hostname);
		strcpy(hwalk->ip, hostip);
		hwalk->logs = NULL;
		hwalk->next = hosts;
		hosts = hwalk;
	}
	for (twalk = tests; (twalk && strcasecmp(testname, twalk->testname)); twalk = twalk->next);
	if (createlog && (twalk == NULL)) {
		twalk = (hobbitd_testlist_t *)malloc(sizeof(hobbitd_testlist_t));
		twalk->testname = strdup(testname);
		twalk->next = tests;
		tests = twalk;
	}
	for (owalk = origins; (owalk && strcasecmp(origin, owalk->name)); owalk = owalk->next);
	if (createlog && (owalk == NULL)) {
		owalk = (htnames_t *)malloc(sizeof(htnames_t));
		owalk->name = strdup(origin);
		origins = owalk;
	}
	if (hwalk && twalk && owalk) {
		for (lwalk = hwalk->logs; (lwalk && ((lwalk->test != twalk) || (lwalk->origin != owalk))); lwalk = lwalk->next);
		if (createlog && (lwalk == NULL)) {
			lwalk = (hobbitd_log_t *)malloc(sizeof(hobbitd_log_t));
			lwalk->color = lwalk->oldcolor = NO_COLOR;
			lwalk->activealert = 0;
			lwalk->testflags = NULL;
			lwalk->sender[0] = '\0';
			lwalk->host = hwalk;
			lwalk->test = twalk;
			lwalk->origin = owalk;
			lwalk->message = NULL;
			lwalk->msgsz = 0;
			lwalk->dismsg = lwalk->ackmsg = NULL;
			lwalk->lastchange = lwalk->validtime = lwalk->enabletime = lwalk->acktime = 0;
			lwalk->cookie = -1;
			lwalk->cookieexpires = 0;
			lwalk->metas = NULL;
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
	xfree(firstline);

	MEMUNDEFINE(hostip);
}


hobbitd_log_t *find_cookie(int cookie)
{
	/*
	 * Find a cookie we have issued.
	 */
	hobbitd_log_t *result = NULL;
	hobbitd_hostlist_t *hwalk = NULL;
	hobbitd_log_t *lwalk = NULL;
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


void handle_status(unsigned char *msg, char *sender, char *hostname, char *testname, hobbitd_log_t *log, int newcolor)
{
	int validity = 30;	/* validity is counted in minutes */
	time_t now = time(NULL);
	int msglen, issummary;
	enum alertstate_t oldalertstatus, newalertstatus;

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
		if (log->dismsg) { xfree(log->dismsg); log->dismsg = NULL; }
		posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);
	}

	if (log->acktime) {
		/* Handling of ack'ed tests */

		if (decide_alertstate(newcolor) == A_OK) {
			/* The test recovered. Clear the ack. */
			log->acktime = 0;
		}

		if (log->acktime > now) {
			/* Dont need to do anything about an acked test */
		}
		else {
			/* The acknowledge has expired. Clear the timestamp and the message buffer */
			log->acktime = 0;
			if (log->ackmsg) { xfree(log->ackmsg); log->ackmsg = NULL; }
		}
	}

	log->logtime = now;

	/*
	 * Decide how long this status is valid.
	 *
	 * Normally we'll just set the valid time according 
	 * to the validity of the status report.
	 *
	 * If the status is acknowledged, make it valid for the longest period
	 * of the acknowledgment and the normal validity (so an acknowledged status
	 * does not go purple because it is not being updated due to the host being down).
	 *
	 * Same tweak must be done for disabled tests.
	 */
	log->validtime = now + validity*60;
	if (log->acktime    && (log->acktime > log->validtime))    log->validtime = log->acktime;
	if (log->enabletime && (log->enabletime > log->validtime)) log->validtime = log->enabletime;

	strncpy(log->sender, sender, sizeof(log->sender)-1);
	*(log->sender + sizeof(log->sender) - 1) = '\0';
	log->oldcolor = log->color;
	log->color = newcolor;
	oldalertstatus = decide_alertstate(log->oldcolor);
	newalertstatus = decide_alertstate(newcolor);

	if (msg != log->message) { /* They can be the same when called from handle_enadis() or check_purple_status() */
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
	if (newalertstatus == A_ALERT) {
		if (log->cookieexpires < now) {
			int newcookie;

			/* Need to ensure that cookies are unique, hence the loop */
			log->cookie = -1; log->cookieexpires = 0;
			do {
				newcookie = (random() % 1000000);
			} while (find_cookie(newcookie));

			log->cookie = newcookie;

			/*
			 * This is fundamentally flawed. The cookie should be generated by
			 * the alert module, because it may not be sent to the user for
			 * a long time, depending on the alert configuration.
			 * That's for 4.1 - for now, we'll just give it a long enough 
			 * lifetime so that cookies will be valid.
			 */
			log->cookieexpires = now + 86400; /* Valid for 1 day */
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
		 * Change of color always goes to the status-change channel.
		 */
		dprintf("posting to stachg channel\n");
		posttochannel(stachgchn, channelnames[C_STACHG], msg, sender, hostname, log, NULL);
		log->lastchange = time(NULL);
	}

	if (!issummary) {
		if (newalertstatus == A_ALERT) {
			/* Status is critical, send alerts */
			dprintf("posting alert to page channel\n");

			log->activealert = 1;
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, log, NULL);
		}
		else if (log->activealert && (oldalertstatus != A_OK) && (newalertstatus == A_OK)) {
			/* Status has recovered, send recovery notice */
			dprintf("posting recovery to page channel\n");

			log->activealert = 0;
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, log, NULL);
		}
		else if (log->activealert && (log->oldcolor != newcolor)) {
			/* 
			 * Status is in-between critical and recovered, but we do have an
			 * active alert for this status. So tell the pager module that the
			 * color has changed.
			 */
			dprintf("posting color change to page channel\n");
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, log, NULL);
		}
	}

	dprintf("posting to status channel\n");
	posttochannel(statuschn, channelnames[C_STATUS], msg, sender, hostname, log, NULL);
	return;
}

void handle_meta(char *msg, hobbitd_log_t *log)
{
	/*
	 * msg has the format "meta HOST.TEST metaname\nmeta-value\n"
	 */
	char *metaname = NULL, *eoln, *line1 = NULL;
	htnames_t *nwalk;
	hobbitd_meta_t *mwalk;

	eoln = strchr(msg, '\n'); 
	if (eoln) {
		char *tok;

		*eoln = '\0'; 
		line1 = strdup(msg);
		*eoln = '\n';

		tok = strtok(line1, " ");		/* "meta" */
		if (tok) tok = strtok(NULL, " ");	/* "host.test" */
		if (tok) tok = strtok(NULL, " ");	/* metaname */
		if (tok) metaname = tok;
	}
	if (!metaname) {
		if (line1) xfree(line1);
		errprintf("Malformed 'meta' message: '%s'\n", msg);
		return;
	}

	for (nwalk = metanames; (nwalk && strcmp(nwalk->name, metaname)); nwalk = nwalk->next) ;
	if (nwalk == NULL) {
		nwalk = (htnames_t *)malloc(sizeof(htnames_t));
		nwalk->name = strdup(metaname);
		nwalk->next = metanames;
		metanames = nwalk;
	}

	for (mwalk = log->metas; (mwalk && (mwalk->metaname != nwalk)); mwalk = mwalk->next);
	if (mwalk == NULL) {
		mwalk = (hobbitd_meta_t *)malloc(sizeof(hobbitd_meta_t));
		mwalk->metaname = nwalk;
		mwalk->value = strdup(eoln+1);
		mwalk->next = log->metas;
		log->metas = mwalk;
	}
	else {
		if (mwalk->value) xfree(mwalk->value);
		mwalk->value = strdup(eoln+1);
	}

	if (line1) xfree(line1);
}

void handle_data(char *msg, char *sender, char *origin, char *hostname, char *testname)
{
	char *chnbuf = (char *)malloc(strlen(origin) + strlen(hostname) + strlen(testname) + strlen(msg) + 4);

	sprintf(chnbuf, "%s|%s|%s\n%s",
		origin, hostname, testname, msg);
	posttochannel(datachn, channelnames[C_DATA], msg, sender, hostname, NULL, chnbuf);
	xfree(chnbuf);
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
	hobbitd_hostlist_t *hwalk = NULL;
	hobbitd_testlist_t *twalk = NULL;
	hobbitd_log_t *log;
	char *p;
	int maybedown;

	MEMDEFINE(firstline);
	MEMDEFINE(hosttest);
	MEMDEFINE(hostip);
	MEMDEFINE(durstr);

	p = strchr(msg, '\n'); 
	if (p == NULL) {
		strncpy(firstline, msg, sizeof(firstline)-1);
	}
	else {
		*p = '\0';
		strncpy(firstline, msg, sizeof(firstline)-1);
		*p = '\n';
	}
	*(firstline + sizeof(firstline) - 1) = '\0';
	assignments = sscanf(firstline, "%*s %199s %99s", hosttest, durstr);
	if (assignments < 1) goto done;
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
		if (p == NULL) goto done; /* "enable foo" ... surely you must be joking. */
		*p = '\0';
		tname = (p+1);
	}
	p = hosttest; while ((p = strchr(p, ',')) != NULL) *p = '.';


	p = knownhost(hosttest, hostip, ghosthandling, &maybedown);
	if (p == NULL) goto done;
	strcpy(hosttest, p);

	for (hwalk = hosts; (hwalk && strcasecmp(hosttest, hwalk->hostname)); hwalk = hwalk->next) ;
	if (hwalk == NULL) {
		/* Unknown host */
		goto done;
	}

	if (tname) {
		for (twalk = tests; (twalk && strcasecmp(tname, twalk->testname)); twalk = twalk->next);
		if (twalk == NULL) {
			/* Unknown test */
			goto done;
		}
	}

	if (enabled) {
		/* Enable is easy - just clear the enabletime */
		if (alltests) {
			for (log = hwalk->logs; (log); log = log->next) {
				log->enabletime = 0;
				if (log->dismsg) {
					xfree(log->dismsg);
					log->dismsg = NULL;
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);
			}
		}
		else {
			for (log = hwalk->logs; (log && (log->test != twalk)); log = log->next) ;
			if (log) {
				log->enabletime = 0;
				if (log->dismsg) {
					xfree(log->dismsg);
					log->dismsg = NULL;
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);
			}
		}
	}
	else {
		/* disable code goes here */
		time_t expires = time(NULL) + duration*60;
		char *dismsg;

		p = hosttest; while ((p = strchr(p, '.')) != NULL) *p = ',';

		dismsg = msg;
		while (*dismsg && !isspace((int)*dismsg)) dismsg++;       /* Skip "disable".... */
		while (*dismsg && isspace((int)*dismsg)) dismsg++;        /* and the space ... */
		while (*dismsg && !isspace((int)*dismsg)) dismsg++;       /* and the host.test ... */
		while (*dismsg && isspace((int)*dismsg)) dismsg++;        /* and the space ... */

		if (alltests) {
			for (log = hwalk->logs; (log); log = log->next) {
				log->enabletime = log->validtime = expires;
				if (dismsg) {
					if (log->dismsg) xfree(log->dismsg);
					log->dismsg = strdup(dismsg);
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);
				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->testname, log, COL_BLUE);
			}
		}
		else {
			for (log = hwalk->logs; (log && (log->test != twalk)); log = log->next) ;
			if (log) {
				log->enabletime = log->validtime = expires;
				if (dismsg) {
					if (log->dismsg) xfree(log->dismsg);
					log->dismsg = strdup(dismsg);
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);

				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->testname, log, COL_BLUE);
			}
		}

	}

done:
	MEMUNDEFINE(firstline);
	MEMUNDEFINE(hosttest);
	MEMUNDEFINE(hostip);
	MEMUNDEFINE(durstr);

	return;
}


void handle_ack(char *msg, char *sender, hobbitd_log_t *log, int duration)
{
	char *p;

	log->acktime = time(NULL)+duration*60;
	if (log->validtime < log->acktime) log->validtime = log->acktime;

	p = msg;
	p += strspn(p, " \t");			/* Skip the space ... */
	p += strspn(p, "-0123456789");		/* and the cookie ... */
	p += strspn(p, " \t");			/* and the space ... */
	p += strspn(p, "0123456789hdwmy");	/* and the duration ... */
	p += strspn(p, " \t");			/* and the space ... */
	log->ackmsg = strdup(p);

	/* Tell the pagers */
	posttochannel(pagechn, "ack", log->ackmsg, sender, log->host->hostname, log, NULL);
	return;
}

void handle_notify(char *msg, char *sender, hobbitd_log_t *log)
{
	char *msgtext = msg_data(msg);

	/* Tell the pagers */
	posttochannel(pagechn, "notify", msgtext, sender, log->host->hostname, log, NULL);
	return;
}

void free_log_t(hobbitd_log_t *zombie)
{
	hobbitd_meta_t *mwalk, *mtmp;

	mwalk = zombie->metas;
	while (mwalk) {
		mtmp = mwalk;
		mwalk = mwalk->next;

		if (mtmp->value) xfree(mtmp->value);
		xfree(mtmp);
	}

	if (zombie->message) xfree(zombie->message);
	if (zombie->dismsg) xfree(zombie->dismsg);
	if (zombie->ackmsg) xfree(zombie->ackmsg);
	xfree(zombie);
}

void handle_dropnrename(enum droprencmd_t cmd, char *sender, char *hostname, char *n1, char *n2)
{
	int maybedown;
	char hostip[20];
	hobbitd_hostlist_t *hwalk;
	hobbitd_testlist_t *twalk, *newt;
	hobbitd_log_t *lwalk;
	char msgbuf[MAXMSG];
	char *marker = NULL;
	char *canonhostname;

	MEMDEFINE(hostip);
	MEMDEFINE(msgbuf);

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
	  case CMD_DROPSTATE:
		marker = "dropstate";
		sprintf(msgbuf, "%s", hostname);
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
	 * NB: knownhost() may return NULL, if the bb-hosts file was re-loaded before
	 * we got around to cleaning up a host.
	 */
	canonhostname = knownhost(hostname, hostip, ghosthandling, &maybedown);
	if (canonhostname) hostname = canonhostname;

	for (hwalk = hosts; (hwalk && strcasecmp(hostname, hwalk->hostname)); hwalk = hwalk->next) ;
	if (hwalk == NULL) goto done;

	switch (cmd) {
	  case CMD_DROPTEST:
		for (twalk = tests; (twalk && strcasecmp(n1, twalk->testname)); twalk = twalk->next) ;
		if (twalk == NULL) goto done;

		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next) ;
		if (lwalk == NULL) goto done;
		if (lwalk == hwalk->logs) {
			hwalk->logs = hwalk->logs->next;
		}
		else {
			hobbitd_log_t *plog;
			for (plog = hwalk->logs; (plog->next != lwalk); plog = plog->next) ;
			plog->next = lwalk->next;
		}
		free_log_t(lwalk);
		break;

	  case CMD_DROPHOST:
	  case CMD_DROPSTATE:
		/* Unlink the hostlist entry */
		if (hwalk == hosts) {
			hosts = hosts->next;
		}
		else {
			hobbitd_hostlist_t *phost;

			for (phost = hosts; (phost->next != hwalk); phost = phost->next) ;
			phost->next = hwalk->next;
		}

		/* Loop through the host logs and free them */
		lwalk = hwalk->logs;
		while (lwalk) {
			hobbitd_log_t *tmp = lwalk;
			lwalk = lwalk->next;

			free_log_t(tmp);
		}

		/* Free the hostlist entry */
		xfree(hwalk);
		break;

	  case CMD_RENAMEHOST:
		if (strlen(hwalk->hostname) <= strlen(n1)) {
			strcpy(hwalk->hostname, n1);
		}
		else {
			xfree(hwalk->hostname);
			hwalk->hostname = strdup(n1);
		}
		break;

	  case CMD_RENAMETEST:
		for (twalk = tests; (twalk && strcasecmp(n1, twalk->testname)); twalk = twalk->next) ;
		if (twalk == NULL) goto done;
		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next) ;
		if (lwalk == NULL) goto done;
		for (newt = tests; (newt && strcasecmp(n2, newt->testname)); newt = newt->next) ;
		if (newt == NULL) {
			newt = (hobbitd_testlist_t *) malloc(sizeof(hobbitd_testlist_t));
			newt->testname = strdup(n2);
			newt->next = tests;
			tests = newt;
		}
		lwalk->test = newt;
		break;
	}

done:
	MEMUNDEFINE(hostip);
	MEMUNDEFINE(msgbuf);

	return;
}


int get_config(char *fn, conn_t *msg)
{
	char fullfn[PATH_MAX];
	FILE *fd = NULL;
	int done = 0;
	int n;

	sprintf(fullfn, "%s/etc/%s", xgetenv("BBHOME"), fn);
	fd = stackfopen(fullfn, "r");
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

char *timestr(time_t tstamp)
{
	static char result[30];
	char *p;

	MEMDEFINE(result);

	if (tstamp == 0) {
		MEMUNDEFINE(result);
		return "N/A";
	}

	strcpy(result, ctime(&tstamp));
	p = strchr(result, '\n'); if (p) *p = '\0';

	MEMUNDEFINE(result);
	return result;
}

void setup_filter(char *buf, char **spage, char **shost, char **stest, int *scolor, char **fields)
{
	char *tok, *s;
	int idx = 0;

	*spage = *shost = *stest = *fields = NULL;
	*scolor = -1;

	tok = strtok(buf, " \t\r\n");
	if (tok) tok = strtok(NULL, " \t\r\n");
	while (tok) {
		/* Get filter */
		if (strncmp(tok, "page=", 5) == 0) {
			*spage = tok+5;
			if (strlen(*spage) == 0) *spage = NULL;
		}
		else if (strncmp(tok, "host=", 5) == 0) *shost = tok+5;
		else if (strncmp(tok, "test=", 5) == 0) *stest = tok+5;
		else if (strncmp(tok, "fields=", 7) == 0) *fields = tok+7;
		else if (strncmp(tok, "color=", 6) == 0) *scolor = parse_color(tok+6);

		tok = strtok(NULL, " \t\r\n");
	}

	/* If no fields given, provide the default set. */
	if (*fields == NULL) *fields = "hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,line1";

	s = strdup(*fields);
	tok = strtok(s, ",");
	while (tok) {
		enum boardfield_t i;
		for (i=0; (boardfieldnames[i].name && strcmp(tok, boardfieldnames[i].name)); i++) ;
		if (i < F_LAST) boardfields[idx++] = boardfieldnames[i].id;
		tok = strtok(NULL, ",");
	}
	boardfields[idx++] = F_NONE;

	xfree(s);
}

void do_message(conn_t *msg, char *origin)
{
	hobbitd_hostlist_t *h;
	hobbitd_testlist_t *t;
	hobbitd_log_t *log;
	int color;
	char sender[20];
	time_t now;
	char *msgfrom;

	MEMDEFINE(sender);

	/* Most likely, we will not send a response */
	msg->doingwhat = NOTALK;
	strcpy(sender, inet_ntoa(msg->addr.sin_addr));
	now = time(NULL);

	/* Count statistics */
	update_statistics(msg->buf);

	if (strncmp(msg->buf, "combo\n", 6) == 0) {
		char *currmsg, *nextmsg;

		currmsg = msg->buf+6;
		do {
			nextmsg = strstr(currmsg, "\n\nstatus");
			if (nextmsg) { *(nextmsg+1) = '\0'; nextmsg += 2; }

			/* Pick out the real sender of this message */
			msgfrom = strstr(currmsg, "\nStatus message received from ");
			if (msgfrom) {
				sscanf(msgfrom, "\nStatus message received from %s\n", sender);
				*msgfrom = '\0';
			}

			get_hts(currmsg, sender, origin, &h, &t, &log, &color, 0, 0);
			if (oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, currmsg)) {
				get_hts(currmsg, sender, origin, &h, &t, &log, &color, 1, 1);
				if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
					fprintf(dbgfd, "\n---- combo message from %s ----\n%s---- end message ----\n", sender, currmsg);
					fflush(dbgfd);
				}

				/* Count individual status-messages also */
				update_statistics(currmsg);

				if (log && (color != -1)) {
					handle_status(currmsg, sender, h->hostname, t->testname, log, color);
				}
			}

			currmsg = nextmsg;
		} while (currmsg);
	}
	else if (strncmp(msg->buf, "meta", 4) == 0) {
		char *currmsg, *nextmsg;

		currmsg = msg->buf;
		do {
			nextmsg = strstr(currmsg, "\n\nmeta");
			if (nextmsg) { *(nextmsg+1) = '\0'; nextmsg += 2; }

			get_hts(currmsg, sender, origin, &h, &t, &log, &color, 0, 0);
			if (log && oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, currmsg)) {
				handle_meta(currmsg, log);
			}

			currmsg = nextmsg;
		} while (currmsg);
	}
	else if (strncmp(msg->buf, "status", 6) == 0) {
		msgfrom = strstr(msg->buf, "\nStatus message received from ");
		if (msgfrom) {
			sscanf(msgfrom, "\nStatus message received from %s\n", sender);
			*msgfrom = '\0';
		}

		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, 0, 0);
		if (!oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, msg->buf)) goto done;

		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, 1, 1);
		if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
			fprintf(dbgfd, "\n---- status message from %s ----\n%s---- end message ----\n", sender, msg->buf);
			fflush(dbgfd);
		}
		if (log && (color != -1)) {
			handle_status(msg->buf, sender, h->hostname, t->testname, log, color);
		}
	}
	else if (strncmp(msg->buf, "data", 4) == 0) {
		char tok[MAXMSG];
		char *hostname = NULL, *testname = NULL;
		int maybedown;
		char hostip[20];

		MEMDEFINE(tok); MEMDEFINE(hostip);

		msgfrom = strstr(msg->buf, "\nStatus message received from ");
		if (msgfrom) {
			sscanf(msgfrom, "\nStatus message received from %s\n", sender);
			*msgfrom = '\0';
		}

		if (sscanf(msg->buf, "data %s\n", tok) == 1) {
			if ((testname = strrchr(tok, '.')) != NULL) {
				char *p;
				*testname = '\0'; 
				testname++; 
				p = tok; while ((p = strchr(p, ',')) != NULL) *p = '.';
				hostname = knownhost(tok, hostip, ghosthandling, &maybedown);
				if (hostname == NULL) log_ghost(tok, sender, msg->buf);
			}

			if (!oksender(statussenders, hostip, msg->addr.sin_addr, msg->buf)) {
				MEMUNDEFINE(tok); MEMUNDEFINE(hostip);
				goto done;
			}
			if (hostname && testname) handle_data(msg->buf, sender, origin, hostname, testname);
		}

		MEMUNDEFINE(tok); MEMUNDEFINE(hostip);
	}
	else if (strncmp(msg->buf, "summary", 7) == 0) {
		/* Summaries are always allowed. Or should we ? */
		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, 1, 1);
		if (log && (color != -1)) {
			handle_status(msg->buf, sender, h->hostname, t->testname, log, color);
		}
	}
	else if (strncmp(msg->buf, "notes", 5) == 0) {
		char tok[MAXMSG];
		char *hostname;
		int maybedown;
		char hostip[20];

		MEMDEFINE(tok); MEMDEFINE(hostip);
		if (sscanf(msg->buf, "notes %s\n", tok) == 1) {
			char *p;

			p = tok; while ((p = strchr(p, ',')) != NULL) *p = '.';
			hostname = knownhost(tok, hostip, ghosthandling, &maybedown);

			if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) {
				MEMUNDEFINE(tok); MEMUNDEFINE(hostip);
				goto done;
			}
			if (hostname) handle_notes(msg->buf, sender, hostname);
		}
		MEMUNDEFINE(tok); MEMUNDEFINE(hostip);
	}
	else if (strncmp(msg->buf, "enable", 6) == 0) {
		if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;
		handle_enadis(1, msg->buf, sender);
	}
	else if (strncmp(msg->buf, "disable", 7) == 0) {
		if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;
		handle_enadis(0, msg->buf, sender);
	}
	else if (strncmp(msg->buf, "config", 6) == 0) {
		char conffn[1024];

		if (!oksender(statussenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		MEMDEFINE(conffn);

		if ( (sscanf(msg->buf, "config %1023s", conffn) == 1) &&
		     (strstr("../", conffn) == NULL) && (get_config(conffn, msg) == 0) ) {
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf;
		}

		MEMUNDEFINE(conffn);
	}
	else if (strncmp(msg->buf, "query ", 6) == 0) {
		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, 0, 0);
		if (!oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, msg->buf)) goto done;

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
	else if (strncmp(msg->buf, "hobbitdlog ", 11) == 0) {
		/* 
		 * Request for a single status log
		 * hobbitdlog HOST.TEST
		 *
		 * hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|ackmsg|dismsg
		 */
		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, 0, 0);
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

			xfree(msg->buf);
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
	else if (strncmp(msg->buf, "hobbitdxlog ", 12) == 0) {
		/* 
		 * Request for a single status log in XML format
		 * hobbitdxlog HOST.TEST
		 *
		 */
		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, 0, 0);
		if (log) {
			char *buf, *bufp;
			int bufsz, buflen;
			hobbitd_meta_t *mwalk;

			if (log->message == NULL) {
				errprintf("%s.%s has a NULL message\n", log->host->hostname, log->test->testname);
				log->message = strdup("");
			}

			bufsz = 4096 + strlen(log->message);
			if (log->ackmsg) bufsz += strlen(log->ackmsg);
			if (log->dismsg) bufsz += strlen(log->dismsg);

			xfree(msg->buf);
			bufp = buf = (char *)malloc(bufsz);
			buflen = 0;

			bufp += sprintf(bufp, "<?xml version='1.0' encoding='ISO-8859-1'?>\n");
			bufp += sprintf(bufp, "<ServerStatus>\n");
			bufp += sprintf(bufp, "  <ServerName>%s</ServerName>\n", h->hostname);
			bufp += sprintf(bufp, "  <Type>%s</Type>\n", log->test->testname);
			bufp += sprintf(bufp, "  <Status>%s</Status>\n", colnames[log->color]);
			bufp += sprintf(bufp, "  <TestFlags>%s</TestFlags>\n", (log->testflags ? log->testflags : ""));
			bufp += sprintf(bufp, "  <LastChange>%s</LastChange>\n", timestr(log->lastchange));
			bufp += sprintf(bufp, "  <LogTime>%s</LogTime>\n", timestr(log->logtime));
			bufp += sprintf(bufp, "  <ValidTime>%s</ValidTime>\n", timestr(log->validtime));
			bufp += sprintf(bufp, "  <AckTime>%s</AckTime>\n", timestr(log->acktime));
			bufp += sprintf(bufp, "  <DisableTime>%s</DisableTime>\n", timestr(log->enabletime));
			bufp += sprintf(bufp, "  <Sender>%s</Sender>\n", log->sender);

			if (log->cookie > 0)
				bufp += sprintf(bufp, "  <Cookie>%d</Cookie>\n", log->cookie);
			else
				bufp += sprintf(bufp, "  <Cookie>N/A</Cookie>\n");

			if (log->ackmsg && (log->acktime > now))
				bufp += sprintf(bufp, "  <AckMsg><![CDATA[%s]]></AckMsg>\n", log->ackmsg);
			else
				bufp += sprintf(bufp, "  <AckMsg>N/A</AckMsg>\n");

			if (log->dismsg && (log->enabletime > now))
				bufp += sprintf(bufp, "  <DisMsg><![CDATA[%s]]></DisMsg>\n", log->dismsg);
			else
				bufp += sprintf(bufp, "  <DisMsg>N/A</DisMsg>\n");

			bufp += sprintf(bufp, "  <Message><![CDATA[%s]]></Message>\n", msg_data(log->message));
			for (mwalk = log->metas; (mwalk); mwalk = mwalk->next) {
				bufp += sprintf(bufp, "<%s>\n%s</%s>\n", 
						mwalk->metaname->name, mwalk->value, mwalk->metaname->name);
			}
			bufp += sprintf(bufp, "</ServerStatus>\n");

			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf = buf;
			msg->buflen = (bufp - buf);
		}
	}
	else if (strncmp(msg->buf, "hobbitdboard", 12) == 0) {
		/* 
		 * Request for a summmary of all known status logs
		 *
		 * hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|1st line of message
		 */
		hobbitd_hostlist_t *hwalk;
		hobbitd_log_t *lwalk, *firstlog;
		hobbitd_log_t infologrec, rrdlogrec;
		hobbitd_testlist_t infotestrec, rrdtestrec;
		char *buf, *bufp;
		int bufsz;
		char *spage = NULL, *shost = NULL, *stest = NULL, *fields = NULL;
		int scolor = -1;
		namelist_t *hi = NULL;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf, &spage, &shost, &stest, &scolor, &fields);
		bufsz = 16384;
		bufp = buf = (char *)malloc(bufsz);

		/* Setup fake log-records for the "info" and "trends" data. */
		infotestrec.testname = xgetenv("INFOCOLUMN");
		infotestrec.next = NULL;
		memset(&infologrec, 0, sizeof(infologrec));
		infologrec.test = &infotestrec;

		rrdtestrec.testname = xgetenv("LARRDCOLUMN");
		rrdtestrec.next = NULL;
		memset(&rrdlogrec, 0, sizeof(rrdlogrec));
		rrdlogrec.test = &rrdtestrec;

		infologrec.color = rrdlogrec.color = COL_GREEN;
		infologrec.message = rrdlogrec.message = "";

		for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
			namelist_t *hinfo;

			/* Host pagename filter */
			if (spage) {
				hi = hostinfo(hwalk->hostname);
				if (hi && (strncmp(hi->page->pagepath, spage, strlen(spage)) != 0)) continue;
			}

			/* Hostname filter */
			if (shost && (strcmp(hwalk->hostname, shost) != 0)) continue;

			/* Handle NOINFO and NOTRENDS here */
			hinfo = hostinfo(hwalk->hostname);
			infologrec.next = &rrdlogrec;
			rrdlogrec.next = hwalk->logs;
			firstlog = &infologrec;
			if (bbh_item(hinfo, BBH_FLAG_NOINFO)) firstlog = firstlog->next;
			if (bbh_item(hinfo, BBH_FLAG_NOTRENDS)) firstlog = firstlog->next;

			for (lwalk = firstlog; (lwalk); lwalk = lwalk->next) {
				char *eoln;
				int f_idx;

				/* Testname filter */
				if (stest && (strcmp(lwalk->test->testname, stest) != 0)) continue;

				/* Color filter */
				if ((scolor != -1) && (lwalk->color != scolor)) continue;

				if (lwalk->message == NULL) {
					errprintf("%s.%s has a NULL message\n", lwalk->host->hostname, lwalk->test->testname);
					lwalk->message = strdup("");
				}

				for (f_idx = 0; (boardfields[f_idx] != F_NONE); f_idx++) {
					int needed = 1024;

					switch (boardfields[f_idx]) {
					  case F_ACKMSG: if (lwalk->ackmsg) needed = 2*strlen(lwalk->ackmsg); break;
					  case F_DISMSG: if (lwalk->dismsg) needed = 2*strlen(lwalk->dismsg); break;
					  case F_MSG: needed = 2*strlen(lwalk->message); break;
					  default: break;
					}

					if ((bufsz - (bufp - buf)) < needed) {
						int buflen = (bufp - buf);
						bufsz += 4096 + needed;
						buf = (char *)realloc(buf, bufsz);
						bufp = buf + buflen;
					}

					if (f_idx > 0) bufp += sprintf(bufp, "|");

					switch (boardfields[f_idx]) {
					  case F_NONE: break;
					  case F_HOSTNAME: bufp += sprintf(bufp, "%s", hwalk->hostname); break;
					  case F_TESTNAME: bufp += sprintf(bufp, "%s", lwalk->test->testname); break;
					  case F_COLOR: bufp += sprintf(bufp, "%s", colnames[lwalk->color]); break;
					  case F_FLAGS: bufp += sprintf(bufp, "%s", (lwalk->testflags ? lwalk->testflags : "")); break;
					  case F_LASTCHANGE: bufp += sprintf(bufp, "%d", (int)lwalk->lastchange); break;
					  case F_LOGTIME: bufp += sprintf(bufp, "%d", (int)lwalk->logtime); break;
					  case F_VALIDTIME: bufp += sprintf(bufp, "%d", (int)lwalk->validtime); break;
					  case F_ACKTIME: bufp += sprintf(bufp, "%d", (int)lwalk->acktime); break;
					  case F_DISABLETIME: bufp += sprintf(bufp, "%d", (int)lwalk->enabletime); break;
					  case F_SENDER: bufp += sprintf(bufp, "%s", lwalk->sender); break;
					  case F_COOKIE: bufp += sprintf(bufp, "%d", lwalk->cookie); break;

					  case F_LINE1:
						eoln = strchr(lwalk->message, '\n'); if (eoln) *eoln = '\0';
						bufp += sprintf(bufp, "%s", msg_data(lwalk->message));
						if (eoln) *eoln = '\n';
						break;

					  case F_ACKMSG: if (lwalk->ackmsg) bufp += sprintf(bufp, "%s", nlencode(lwalk->ackmsg)); break;
					  case F_DISMSG: if (lwalk->dismsg) bufp += sprintf(bufp, "%s", nlencode(lwalk->dismsg)); break;
					  case F_MSG: bufp += sprintf(bufp, "%s", nlencode(lwalk->message)); break;
					  case F_LAST: break;
					}
				}
				bufp += sprintf(bufp, "\n");
			}
		}
		*bufp = '\0';

		xfree(msg->buf);
		msg->doingwhat = RESPONDING;
		msg->bufp = msg->buf = buf;
		msg->buflen = (bufp - buf);
	}
	else if (strncmp(msg->buf, "hobbitdxboard", 13) == 0) {
		/* 
		 * Request for a summmary of all known status logs in XML format
		 *
		 */
		hobbitd_hostlist_t *hwalk;
		hobbitd_log_t *lwalk;
		char *buf, *bufp;
		int bufsz;
		char *spage = NULL, *shost = NULL, *stest = NULL, *fields = NULL;
		int scolor = -1;
		namelist_t *hi = NULL;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf, &spage, &shost, &stest, &scolor, &fields);
		bufsz = 16384;
		bufp = buf = (char *)malloc(bufsz);

		bufp += sprintf(bufp, "<?xml version='1.0' encoding='ISO-8859-1'?>\n");
		bufp += sprintf(bufp, "<StatusBoard>\n");

		for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {

			/* Host pagename filter */
			if (spage) {
				hi = hostinfo(hwalk->hostname);
				if (hi && (strncmp(hi->page->pagepath, spage, strlen(spage)) != 0)) continue;
			}

			/* Hostname filter */
			if (shost && (strcmp(hwalk->hostname, shost) != 0)) continue;

			for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
				char *eoln;
				int buflen = (bufp - buf);
				
				/* Testname filter */
				if (stest && (strcmp(lwalk->test->testname, stest) != 0)) continue;

				/* Color filter */
				if ((scolor != -1) && (lwalk->color != scolor)) continue;

				if (lwalk->message == NULL) {
					errprintf("%s.%s has a NULL message\n", lwalk->host->hostname, lwalk->test->testname);
					lwalk->message = strdup("");
				}

				eoln = strchr(lwalk->message, '\n');
				if (eoln) *eoln = '\0';
				if ((bufsz - buflen - strlen(lwalk->message)) < 4096) {
					bufsz += (16384 + strlen(lwalk->message));
					buf = (char *)realloc(buf, bufsz);
					bufp = buf + buflen;
				}

				bufp += sprintf(bufp, "  <ServerStatus>\n");
				bufp += sprintf(bufp, "    <ServerName>%s</ServerName>\n", hwalk->hostname);
				bufp += sprintf(bufp, "    <Type>%s</Type>\n", lwalk->test->testname);
				bufp += sprintf(bufp, "    <Status>%s</Status>\n", colnames[lwalk->color]);
				bufp += sprintf(bufp, "    <TestFlags>%s</TestFlags>\n", (lwalk->testflags ? lwalk->testflags : ""));
				bufp += sprintf(bufp, "    <LastChange>%s</LastChange>\n", timestr(lwalk->lastchange));
				bufp += sprintf(bufp, "    <LogTime>%s</LogTime>\n", timestr(lwalk->logtime));
				bufp += sprintf(bufp, "    <ValidTime>%s</ValidTime>\n", timestr(lwalk->validtime));
				bufp += sprintf(bufp, "    <AckTime>%s</AckTime>\n", timestr(lwalk->acktime));
				bufp += sprintf(bufp, "    <DisableTime>%s</DisableTime>\n", timestr(lwalk->enabletime));
				bufp += sprintf(bufp, "    <Sender>%s</Sender>\n", lwalk->sender);

				if (lwalk->cookie > 0)
					bufp += sprintf(bufp, "    <Cookie>%d</Cookie>\n", lwalk->cookie);
				else
					bufp += sprintf(bufp, "    <Cookie>N/A</Cookie>\n");

				bufp += sprintf(bufp, "    <MessageSummary><![CDATA[%s]]></MessageSummary>\n", lwalk->message);
				bufp += sprintf(bufp, "  </ServerStatus>\n");
				if (eoln) *eoln = '\n';
			}
		}
		bufp += sprintf(bufp, "</StatusBoard>\n");

		xfree(msg->buf);
		msg->doingwhat = RESPONDING;
		msg->bufp = msg->buf = buf;
		msg->buflen = (bufp - buf);
	}
	else if ((strncmp(msg->buf, "hobbitdack", 10) == 0) || (strncmp(msg->buf, "ack ack_event", 13) == 0)) {
		/* hobbitdack COOKIE DURATION TEXT */
		char *p;
		int cookie, duration;
		char durstr[100];
		hobbitd_log_t *lwalk;

		if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		MEMDEFINE(durstr);

		/*
		 * For just a bit of compatibility with the old BB system,
		 * we will accept an "ack ack_event" message. This allows us
		 * to work with existing acknowledgement scripts.
		 */
		if (strncmp(msg->buf, "hobbitdack", 10) == 0) p = msg->buf + 10;
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
			else {
				errprintf("Cookie %d not found, dropping ack\n", cookie);
			}
		}
		else {
			errprintf("Bogus ack message from %s: '%s'\n", sender, msg->buf);
		}

		MEMUNDEFINE(durstr);
	}
	else if (strncmp(msg->buf, "drop ", 5) == 0) {
		char hostname[200];
		char testname[200];
		int n;

		if (!oksender(adminsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		MEMDEFINE(hostname); MEMDEFINE(testname);

		n = sscanf(msg->buf, "drop %199s %199s", hostname, testname);
		if (n == 1) {
			handle_dropnrename(CMD_DROPHOST, sender, hostname, NULL, NULL);
		}
		else if (n == 2) {
			handle_dropnrename(CMD_DROPTEST, sender, hostname, testname, NULL);
		}

		MEMUNDEFINE(hostname); MEMUNDEFINE(testname);
	}
	else if (strncmp(msg->buf, "rename ", 7) == 0) {
		char hostname[200];
		char n1[200], n2[200];
		int n;

		if (!oksender(adminsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		MEMDEFINE(hostname); MEMDEFINE(n1); MEMDEFINE(n2);

		n = sscanf(msg->buf, "rename %199s %199s %199s", hostname, n1, n2);
		if (n == 2) {
			/* Host rename */
			handle_dropnrename(CMD_RENAMEHOST, sender, hostname, n1, NULL);
		}
		else if (n == 3) {
			/* Test rename */
			handle_dropnrename(CMD_RENAMETEST, sender, hostname, n1, n2);
		}

		MEMUNDEFINE(hostname); MEMUNDEFINE(n1); MEMUNDEFINE(n2);
	}
	else if (strncmp(msg->buf, "dummy", 5) == 0) {
		/* Do nothing */
	}
	else if (strncmp(msg->buf, "notify", 6) == 0) {
		if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;
		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, 0, 0);
		if (log) handle_notify(msg->buf, sender, log);
	}
	else if (strncmp(msg->buf, "schedule", 8) == 0) {
		char *cmd;

		/*
		 * Schedule a later command. This is either
		 * "schedule" - no params: list the currently scheduled commands.
		 * "schedule TIME COMMAND": Add a COMMAND to run at TIME.
		 * "schedule cancel ID": Cancel the scheduled command with id ID.
		 */
		cmd = msg->buf + 8; cmd += strspn(cmd, " ");

		if (strlen(cmd) == 0) {
			char *buf, *bufp;
			int bufsz, buflen;
			scheduletask_t *swalk;

			bufsz = 4096;
			bufp = buf = (char *)malloc(bufsz);
			*buf = '\0'; buflen = 0;

			for (swalk = schedulehead; (swalk); swalk = swalk->next) {
				int needed = 128 + strlen(swalk->command);

				if ((bufsz - (bufp - buf)) < needed) {
					int buflen = (bufp - buf);
					bufsz += 4096 + needed;
					buf = (char *)realloc(buf, bufsz);
					bufp = buf + buflen;
				}

				bufp += sprintf(bufp, "%d|%d|%s|%s\n", swalk->id,
						(int)swalk->executiontime, swalk->sender, nlencode(swalk->command));
			}

			xfree(msg->buf);
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf = buf;
			msg->buflen = (bufp - buf);
		}
		else {
			if (strncmp(cmd, "cancel", 6) != 0) {
				scheduletask_t *newitem = (scheduletask_t *)malloc(sizeof(scheduletask_t));

				newitem->id = nextschedid++;
				newitem->executiontime = (time_t) atoi(cmd);
				cmd += strspn(cmd, "0123456789");
				cmd += strspn(cmd, " ");
				newitem->sender = strdup(sender);
				newitem->command = strdup(cmd);
				newitem->next = schedulehead;
				schedulehead = newitem;
			}
			else {
				scheduletask_t *swalk, *sprev;
				int id;
				
				id = atoi(cmd + 6);
				swalk = schedulehead; sprev = NULL; 
				while (swalk && (swalk->id != id)) {
					sprev = swalk; 
					swalk = swalk->next;
				}

				if (swalk) {
					xfree(swalk->sender);
					xfree(swalk->command);
					if (sprev == NULL) {
						schedulehead = swalk->next;
					}
					else {
						sprev->next = swalk->next;
					}
					xfree(swalk);
				}
			}
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

	MEMUNDEFINE(sender);
}


void save_checkpoint(void)
{
	char *tempfn;
	FILE *fd;
	hobbitd_hostlist_t *hwalk;
	hobbitd_log_t *lwalk;
	time_t now = time(NULL);
	scheduletask_t *swalk;

	if (checkpointfn == NULL) return;

	dprintf("Start save_checkpoint\n");
	tempfn = malloc(strlen(checkpointfn) + 20);
	sprintf(tempfn, "%s.%d", checkpointfn, (int)now);
	fd = fopen(tempfn, "w");
	if (fd == NULL) {
		errprintf("Cannot open checkpoint file %s\n", tempfn);
		xfree(tempfn);
		return;
	}

	for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
		for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
			if (lwalk->dismsg && (lwalk->enabletime < now)) {
				xfree(lwalk->dismsg);
				lwalk->dismsg = NULL;
				lwalk->enabletime = 0;
			}
			if (lwalk->ackmsg && (lwalk->acktime < now)) {
				xfree(lwalk->ackmsg);
				lwalk->ackmsg = NULL;
				lwalk->acktime = 0;
			}
			fprintf(fd, "@@HOBBITDCHK-V1|%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%s", 
				lwalk->origin->name, hwalk->hostname, lwalk->test->testname, lwalk->sender,
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

	for (swalk = schedulehead; (swalk); swalk = swalk->next) {
		fprintf(fd, "@@HOBBITDCHK-V1|.task.|%d|%d|%s|%s\n", 
			swalk->id, (int)swalk->executiontime, swalk->sender, nlencode(swalk->command));
	}

	fclose(fd);
	rename(tempfn, checkpointfn);
	xfree(tempfn);
	dprintf("End save_checkpoint\n");
}


void load_checkpoint(char *fn)
{
	FILE *fd;
	char l[4*MAXMSG];
	char *item;
	int i, err, maybedown;
	char hostip[20];
	hobbitd_hostlist_t *htail = NULL;
	hobbitd_testlist_t *t = NULL;
	hobbitd_log_t *ltail = NULL;
	htnames_t *origin = NULL;
	char *originname = NULL, *hostname = NULL, *testname = NULL, *sender = NULL, *testflags = NULL; 
	char *statusmsg = NULL, *disablemsg = NULL, *ackmsg = NULL;
	time_t logtime = 0, lastchange = 0, validtime = 0, enabletime = 0, acktime = 0, cookieexpires = 0;
	int color = COL_GREEN, oldcolor = COL_GREEN, cookie = -1;

	fd = fopen(fn, "r");
	if (fd == NULL) {
		errprintf("Cannot access checkpoint file %s for restore\n", fn);
		return;
	}

	MEMDEFINE(l);
	MEMDEFINE(hostip);

	while (fgets(l, sizeof(l)-1, fd)) {
		hostname = testname = sender = testflags = statusmsg = disablemsg = ackmsg = NULL;
		lastchange = validtime = enabletime = acktime = 0;
		err = 0;

		if (strncmp(l, "@@HOBBITDCHK-V1|.task.|", 23) == 0) {
			scheduletask_t *newtask = (scheduletask_t *)calloc(1, sizeof(scheduletask_t));

			item = gettok(l, "|\n"); i = 0;
			while (item && !err) {
				switch (i) {
				  case 0: break;
				  case 1: break;
				  case 2: newtask->id = atoi(item); break;
				  case 3: newtask->executiontime = (time_t) atoi(item); break;
				  case 4: newtask->sender = strdup(item); break;
				  case 5: nldecode(item); newtask->command = strdup(item); break;
				  default: break;
				}
				item = gettok(NULL, "|\n"); i++;
			}

			if (newtask->id && (newtask->executiontime > time(NULL)) && newtask->sender && newtask->command) {
				newtask->next = schedulehead;
				schedulehead = newtask;
			}
			else {
				if (newtask->sender) xfree(newtask->sender);
				if (newtask->command) xfree(newtask->command);
				xfree(newtask);
			}

			continue;
		}

		item = gettok(l, "|\n"); i = 0;
		while (item && !err) {
			switch (i) {
			  case 0: err = ((strcmp(item, "@@HOBBITDCHK-V1") != 0) && (strcmp(item, "@@BBGENDCHK-V1") != 0)); break;
			  case 1: originname = item; break;
			  case 2: if (strlen(item)) hostname = item; else err=1; break;
			  case 3: if (strlen(item)) testname = item; else err=1; break;
			  case 4: sender = item; break;
			  case 5: color = parse_color(item); if (color == -1) err = 1; break;
			  case 6: testflags = item; break;
			  case 7: oldcolor = parse_color(item); if (oldcolor == -1) oldcolor = NO_COLOR; break;
			  case 8: logtime = atoi(item); break;
			  case 9: lastchange = atoi(item); break;
			  case 10: validtime = atoi(item); break;
			  case 11: enabletime = atoi(item); break;
			  case 12: acktime = atoi(item); break;
			  case 13: cookie = atoi(item); break;
			  case 14: cookieexpires = atoi(item); break;
			  case 15: if (strlen(item)) statusmsg = item; else err=1; break;
			  case 16: disablemsg = item; break;
			  case 17: ackmsg = item; break;
			  default: err = 1;
			}

			item = gettok(NULL, "|\n"); i++;
		}

		if (i < 17) {
			errprintf("Too few fields in record - found %d, expected 17\n", i);
			err = 1;
		}

		if (err) continue;

		/* Only load hosts we know; they may have been dropped while we were offline */
		hostname = knownhost(hostname, hostip, ghosthandling, &maybedown);
		if (hostname == NULL) continue;

		/* Ignore the "info" and "trends" data, since we generate on the fly now. */
		if (strcmp(testname, xgetenv("INFOCOLUMN")) == 0) continue;
		if (strcmp(testname, xgetenv("LARRDCOLUMN")) == 0) continue;

		if ((hosts == NULL) || (strcmp(hostname, htail->hostname) != 0)) {
			/* New host */
			if (hosts == NULL) {
				htail = hosts = (hobbitd_hostlist_t *) malloc(sizeof(hobbitd_hostlist_t));
			}
			else {
				htail->next = (hobbitd_hostlist_t *) malloc(sizeof(hobbitd_hostlist_t));
				htail = htail->next;
			}
			htail->hostname = strdup(hostname);
			strcpy(htail->ip, hostip);
			htail->logs = NULL;
			htail->next = NULL;
		}

		for (t=tests; (t && (strcmp(t->testname, testname) != 0)); t = t->next) ;
		if (t == NULL) {
			t = (hobbitd_testlist_t *) malloc(sizeof(hobbitd_testlist_t));
			t->testname = strdup(testname);
			t->next = tests;
			tests = t;
		}

		for (origin=origins; (origin && (strcmp(origin->name, originname) != 0)); origin = origin->next) ;
		if (origin == NULL) {
			origin = (htnames_t *) malloc(sizeof(htnames_t));
			origin->name = strdup(originname);
			origin->next = origins;
			origins = origin;
		}

		if (htail->logs == NULL) {
			ltail = htail->logs = (hobbitd_log_t *) malloc(sizeof(hobbitd_log_t));
		}
		else {
			ltail->next = (hobbitd_log_t *)malloc(sizeof(hobbitd_log_t));
			ltail = ltail->next;
		}

		/* Fixup validtime in case of ack'ed or disabled tests */
		if (validtime < acktime) validtime = acktime;
		if (validtime < enabletime) validtime = enabletime;

		ltail->test = t;
		ltail->host = htail;
		ltail->origin = origin;
		ltail->color = color;
		ltail->oldcolor = oldcolor;
		ltail->activealert = (decide_alertstate(color) == A_ALERT);
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

	MEMUNDEFINE(l);
	MEMDEFINE(hostip);
}


void check_purple_status(void)
{
	hobbitd_hostlist_t *hwalk;
	hobbitd_log_t *lwalk;
	time_t now = time(NULL);

	dprintf("Start check for purple logs\n");
	for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
		lwalk = hwalk->logs;
		while (lwalk) {
			if (lwalk->validtime < now) {
				dprintf("Purple log from %s %s\n", hwalk->hostname, lwalk->test->testname);
				if (strcmp(hwalk->hostname, "summary") == 0) {
					/*
					 * A summary has gone stale. Drop it.
					 */
					hobbitd_log_t *tmp;

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
					hobbitd_log_t *tmp;
					int newcolor = COL_PURPLE;

					/*
					 * See if this is a host where the "conn" test shows it is down.
					 * If yes, then go CLEAR, instead of PURPLE.
					 */
					for (tmp = hwalk->logs; (tmp && strcmp(tmp->test->testname, xgetenv("PINGCOLUMN"))); tmp = tmp->next) ;
					if (tmp) {
						switch (tmp->color) {
						  case COL_RED:
						  case COL_YELLOW:
						  case COL_BLUE:
							newcolor = COL_CLEAR;
							break;

						  default:
							newcolor = COL_PURPLE;
							break;
						}
					}

					handle_status(lwalk->message, "hobbitd", 
						hwalk->hostname, lwalk->test->testname, lwalk, newcolor);
					lwalk = lwalk->next;
				}
			}
			else {
				lwalk = lwalk->next;
			}
		}
	}
	dprintf("End check for purple logs\n");
}

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGCHLD:
		break;

	  case SIGALRM:
		gotalarm = 1;
		break;

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
	}
}


int main(int argc, char *argv[])
{
	conn_t *connhead = NULL, *conntail=NULL;
	char *listenip = "0.0.0.0";
	int listenport = 0;
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
	time_t conn_timeout = 10;
	time_t nextheartbeat = 0;

	MEMDEFINE(colnames);

	boottime = time(NULL);

	colnames[COL_GREEN] = "green";
	colnames[COL_YELLOW] = "yellow";
	colnames[COL_RED] = "red";
	colnames[COL_CLEAR] = "clear";
	colnames[COL_BLUE] = "blue";
	colnames[COL_PURPLE] = "purple";
	colnames[NO_COLOR] = "none";
	gettimeofday(&tv, &tz);
	srandom(tv.tv_usec);

	/* Load alert config */
	alertcolors = colorset(xgetenv("ALERTCOLORS"), ((1 << COL_GREEN) | (1 << COL_BLUE)));
	okcolors = colorset(xgetenv("OKCOLORS"), (1 << COL_RED));

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
		else if (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=') + 1;
			int newconn_timeout = atoi(p);
			if ((newconn_timeout < 5) || (newconn_timeout > 60)) 
				errprintf("--timeout must be between 5 and 60\n");
			else
				conn_timeout = newconn_timeout;
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
		else if (argnmatch(argv[argi], "--ghosts=")) {
			char *p = strchr(argv[argi], '=') + 1;

			if (strcmp(p, "allow") == 0) ghosthandling = 0;
			else if (strcmp(p, "drop") == 0) ghosthandling = 1;
			else if (strcmp(p, "log") == 0) ghosthandling = 2;
		}
		else if (argnmatch(argv[argi], "--no-purple")) {
			do_purples = 0;
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
			/* Who is allowed to send us "drop", "rename", "config", "query" messages */
			char *p = strchr(argv[argi], '=');
			adminsenders = getsenderlist(p+1);
		}
		else if (argnmatch(argv[argi], "--www-senders=")) {
			/* Who is allowed to send us "hobbitdboard", "hobbitdlog"  messages */
			char *p = strchr(argv[argi], '=');
			wwwsenders = getsenderlist(p+1);
		}
		else if (argnmatch(argv[argi], "--dbghost=")) {
			char *p = strchr(argv[argi], '=');

			dbghost = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, NULL);
		}
		else if (argnmatch(argv[argi], "--help")) {
			printf("Options:\n");
			printf("\t--listen=IP:PORT              : The address the daemon listens on\n");
			printf("\t--bbhosts=FILENAME            : The bb-hosts file\n");
			printf("\t--ghosts=allow|drop|log       : How to handle unknown hosts\n");
			printf("\t--alertcolors=COLOR[,COLOR]   : What colors trigger an alert\n");
			printf("\t--okcolors=COLOR[,COLOR]      : What colors trigger an recovery alert\n");
			return 1;
		}
		else {
			errprintf("Unknown option '%s' - ignored\n", argv[argi]);
		}
	}

	if (xgetenv("BBHOSTS") && (bbhostsfn == NULL)) {
		bbhostsfn = strdup(xgetenv("BBHOSTS"));
	}

	if (listenport == 0) {
		if (xgetenv("BBPORT"))
			listenport = atoi(xgetenv("BBPORT"));
		else
			listenport = 1984;
	}

	if (ghosthandling == -1) {
		if (xgetenv("BBGHOSTS")) ghosthandling = atoi(xgetenv("BBGHOSTS"));
		else ghosthandling = 0;
	}

	if (ghosthandling && (bbhostsfn == NULL)) {
		errprintf("No bb-hosts file specified, required when using ghosthandling\n");
		exit(1);
	}

	if (restartfn) {
		errprintf("Loading hostnames\n");
		load_hostnames(bbhostsfn, NULL, get_fqdn());
		errprintf("Loading saved state\n");
		load_checkpoint(restartfn);
	}

	nextcheckpoint = time(NULL) + checkpointinterval;
	nextpurpleupdate = time(NULL) + 600;	/* Wait 10 minutes the first time */
	last_stats_time = time(NULL);	/* delay sending of the first status report until we're fully running */


	/* Set up a socket to listen for new connections */
	errprintf("Setting up network listener on %s:%d\n", listenip, listenport);
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
			/* Parent just exits */
			exit(0);
		}

		/* Child (daemon) continues here */
		setsid();
	}

	if (pidfile == NULL) {
		/* Setup a default pid-file */
		char fn[PATH_MAX];

		sprintf(fn, "%s/hobbitd.pid", xgetenv("BBSERVERLOGS"));
		pidfile = strdup(fn);
	}

	/* Save PID */
	{
		FILE *fd = fopen(pidfile, "w");
		if (fd) {
			if (fprintf(fd, "%d\n", (int)getpid()) <= 0) {
				errprintf("Error writing PID file %s: %s\n", pidfile, strerror(errno));
			}
			fclose(fd);
		}
		else {
			errprintf("Cannot open PID file %s: %s\n", pidfile, strerror(errno));
		}
	}

	errprintf("Setting up signal handlers\n");
	setup_signalhandler("hobbitd");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);

	errprintf("Setting up hobbitd channels\n");
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

	errprintf("Setting up logfiles\n");
	freopen("/dev/null", "r", stdin);
	if (logfn) {
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	if (dbghost) {
		dbgfd = fopen("/tmp/hobbitd.dbg", "a");
		if (dbgfd == NULL) errprintf("Cannot open debug file: %s\n", strerror(errno));
	}

	if (!daemonize) {
		/* Setup to send the parent proces a heartbeat-signal (SIGUSR2) */
		nextheartbeat = time(NULL);
		parentpid = getppid(); if (parentpid <= 1) parentpid = 0;
	}

	errprintf("Setup complete\n");
	do {
		struct timeval seltmo;
		fd_set fdread, fdwrite;
		int maxfd, n;
		conn_t *cwalk;
		time_t now = time(NULL);
		int childstat;

		if (logfn && dologswitch) {
			freopen(logfn, "a", stdout);
			freopen(logfn, "a", stderr);
			dologswitch = 0;
			posttochannel(statuschn, "logrotate", NULL, "hobbitd", NULL, NULL, "");
			posttochannel(stachgchn, "logrotate", NULL, "hobbitd", NULL, NULL, "");
			posttochannel(pagechn, "logrotate", NULL, "hobbitd", NULL, NULL, "");
			posttochannel(datachn, "logrotate", NULL, "hobbitd", NULL, NULL, "");
			posttochannel(noteschn, "logrotate", NULL, "hobbitd", NULL, NULL, "");
			posttochannel(enadischn, "logrotate", NULL, "hobbitd", NULL, NULL, "");
		}

		if (reloadconfig && bbhostsfn) {
			hobbitd_hostlist_t *hwalk, *nexth;

			reloadconfig = 0;
			load_hostnames(bbhostsfn, NULL, get_fqdn());
			for (hwalk = hosts; (hwalk); hwalk = nexth) {
				nexth = hwalk->next;  /* hwalk might disappear */
				if (hostinfo(hwalk->hostname) == NULL) {
					handle_dropnrename(CMD_DROPSTATE, "hobbitd", hwalk->hostname, NULL, NULL);
				}
			}
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

		if ((last_stats_time + 300) <= now) {
			char *buf;
			hobbitd_hostlist_t *h;
			hobbitd_testlist_t *t;
			hobbitd_log_t *log;
			int color;

			buf = generate_stats();
			get_hts(buf, "hobbitd", "", &h, &t, &log, &color, 1, 1);
			if (!h || !t || !log) {
				errprintf("hobbitd servername MACHINE='%s' not listed in bb-hosts, dropping hobbitd status\n",
					  xgetenv("MACHINE"));
			}
			else {
				handle_status(buf, "hobbitd", h->hostname, t->testname, log, color);
			}
			last_stats_time = now;
			flush_errbuf();
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

		if (parentpid && (nextheartbeat <= now)) {
			dprintf("Sending heartbeat to pid %d\n", (int) parentpid);
			nextheartbeat = now + 5;
			kill(parentpid, SIGUSR2);
		}

		seltmo.tv_sec = 5; seltmo.tv_usec = 0;
		n = select(maxfd+1, &fdread, &fdwrite, NULL, &seltmo);
		if (n <= 0) {
			if ((errno == EINTR) || (n == 0)) {
				/* Interrupted or a timeout happened */
				continue;
			}
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
						*(cwalk->bufp) = '\0';

						/* FIXME - need to set origin here */
						do_message(cwalk, "");
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

		/* Any scheduled tasks that need attending to? */
		{
			scheduletask_t *swalk, *sprev;

			swalk = schedulehead; sprev = NULL;
			while (swalk) {
				if (swalk->executiontime <= now) {
					scheduletask_t *runtask = swalk;
					conn_t task;

					/* Unlink the entry */
					if (sprev == NULL) 
						schedulehead = swalk->next;
					else
						sprev->next = swalk->next;
					swalk = swalk->next;

					memset(&task, 0, sizeof(task));
					inet_aton(runtask->sender, (struct in_addr *) &task.addr.sin_addr.s_addr);
					task.buf = task.bufp = runtask->command;
					task.buflen = strlen(runtask->command); task.bufsz = task.buflen+1;
					do_message(&task, "");

					errprintf("Ran scheduled task %d from %s: %s\n", 
						  runtask->id, runtask->sender, runtask->command);
					xfree(runtask->sender); xfree(runtask->command); xfree(runtask);
				}
				else {
					sprev = swalk;
					swalk = swalk->next;
				}
			}
		}

		/* Clean up conn structs that are no longer used */
		{
			conn_t *tmp, *khead;

			now = time(NULL);
			khead = NULL; cwalk = connhead;
			while (cwalk) {
				/* Check for connections that timeout */
				if (now > cwalk->timeout) {
					update_statistics("");
					cwalk->doingwhat = NOTALK;
					if (cwalk->sock >= 0) {
						shutdown(cwalk->sock, SHUT_RDWR);
						close(cwalk->sock);
						cwalk->sock = -1;
					}
				}

				/* Move dead connections to a purge-list */
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

			/* Purge the dead connections */
			while (khead) {
				tmp = khead;
				khead = khead->next;

				if (tmp->buf) xfree(tmp->buf);
				xfree(tmp);
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
				conntail->timeout = now + conn_timeout;
				conntail->next = NULL;
			}
		}

		/* Pickup any finished child processes to avoid zombies */
		while (wait3(&childstat, WNOHANG, NULL) > 0) ;
	} while (running);

	/* Tell the workers we to shutdown also */
	running = 1;   /* Kludge, but it's the only way to get posttochannel to do something. */
	posttochannel(statuschn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(stachgchn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(pagechn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(datachn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(noteschn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(enadischn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
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

	MEMUNDEFINE(colnames);

	return 0;
}

