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

static char rcsid[] = "$Id: hobbitd.c,v 1.190 2006-01-20 11:11:52 henrik Exp $";

#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
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

#include "hobbitd_buffer.h"
#include "hobbitd_ipc.h"

/*
 * The absolute maximum size we'll grow our buffers to accomodate an incoming message.
 * This is really just an upper bound to squash the bad guys trying to data-flood us. 
 */
#define MAX_HOBBIT_INBUFSZ (10*1024*1024)	/* 10 MB */

/* The initial size of an input buffer. Make this large enough for most traffic. */
#define HOBBIT_INBUF_INITIAL   (128*1024)

/* How much the input buffer grows per re-allocation */
#define HOBBIT_INBUF_INCREMENT (32*1024)

/* How long to keep an ack after the status has recovered */
#define ACKCLEARDELAY 720 /* 12 minutes */


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

typedef struct ackinfo_t {
	int level;
	time_t received, validuntil, cleartime;
	char *ackedby;
	char *msg;
	struct ackinfo_t *next;
} ackinfo_t;

/* This holds all information about a single status */
typedef struct hobbitd_log_t {
	struct hobbitd_hostlist_t *host;
	struct hobbitd_testlist_t *test;
	struct htnames_t *origin;
	int color, oldcolor, activealert, histsynced;
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
	ackinfo_t *acklist;	/* Holds list of acks */
	struct hobbitd_log_t *next;
} hobbitd_log_t;

/* This is a list of the hosts we have seen reports for, and links to their status logs */
typedef struct hobbitd_hostlist_t {
	char *hostname;
	char ip[16];
	hobbitd_log_t *logs;
	char *clientmsg;
} hobbitd_hostlist_t;

RbtHandle rbhosts;				/* The hosts we have reports from */
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
sender_t *tracelist = NULL;
int      traceall = 0;
int      ignoretraced = 0;
int      save_clientlogs = 1;

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
static volatile int reloadconfig = 0;
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
hobbitd_channel_t *clientchn = NULL;	/* Receives "client" messages */

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
int  hostcount = 0;

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
	{ "client", 0 },
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
		    F_ACKMSG, F_DISMSG, F_MSG, F_CLIENT,
		    F_ACKLIST,
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
	{ "client", F_CLIENT },
	{ "acklist", F_ACKLIST },
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

static int hostname_compare(void *a, void *b)
{
	return strcasecmp((char *)a, (char *)b);
}

static hobbitd_hostlist_t *gettreeitem(RbtHandle rbhosts, RbtIterator hosthandle)
{
	void *k1, *k2;

	rbtKeyValue(rbhosts, hosthandle, &k1, &k2);
	return (hobbitd_hostlist_t *)k2;
}

void update_statistics(char *cmd)
{
	int i;

	dprintf("-> update_statistics\n");

	if (!cmd) {
		dprintf("No command for update_statistics\n");
		return;
	}

	msgs_total++;

	i = 0;
	while (hobbitd_stats[i].cmd && strncmp(hobbitd_stats[i].cmd, cmd, strlen(hobbitd_stats[i].cmd))) { i++; }
	hobbitd_stats[i].count++;

	dprintf("<- update_statistics\n");
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

	dprintf("-> generate_stats\n");

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
	clients = semctl(clientchn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "client channel messages: %10ld (%d readers)\n", clientchn->msgcount, clients);

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

	dprintf("<- generate_stats\n");

	return statsbuf;
}


sender_t *getsenderlist(char *iplist)
{
	char *p, *tok;
	sender_t *result;
	int count;

	dprintf("-> getsenderlist\n");

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

	dprintf("<- getsenderlist\n");

	return result;
}

int oksender(sender_t *oklist, char *targetip, struct in_addr sender, char *msgbuf)
{
	int i;
	unsigned long int tg_ip;
	char *eoln;

	dprintf("-> oksender\n");

	/* If oklist is empty, we're not doing any access checks - so return OK */
	if (oklist == NULL) {
		dprintf("<- oksender(1-a)\n");
		return 1;
	}

	/* If we know the target, it would be ok for the host to report on itself. */
	if (targetip) {
		if (strcmp(targetip, "0.0.0.0") == 0) return 1; /* DHCP hosts can report from any address */
		tg_ip = ntohl(inet_addr(targetip));
		if (ntohl(sender.s_addr) == tg_ip) {
			dprintf("<- oksender(1-b)\n");
			return 1;
		}
	}

	/* It's someone else reporting about the host. Check the access list */
	i = 0;
	do {
		if ((oklist[i].ipval & oklist[i].ipmask) == (ntohl(sender.s_addr) & oklist[i].ipmask)) {
			dprintf("<- oksender(1-c)\n");
			return 1;
		}
		i++;
	} while (oklist[i].ipval != 0);

	/* Refuse and log the message */
	eoln = strchr(msgbuf, '\n'); if (eoln) *eoln = '\0';
	errprintf("Refused message from %s: %s\n", inet_ntoa(sender), msgbuf);
	if (eoln) *eoln = '\n';

	dprintf("<- oksender(0)\n");

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
	unsigned int bufsz = shbufsz(channel->channelid);

	dprintf("-> posttochannel\n");

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
		n = snprintf(channel->channelbuf, (bufsz-5),
			    "@@%s#%u|%d.%06d|%s|%s", 
			    channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, sender,
			    readymsg);
		if (n > (bufsz-5)) errprintf("Oversize data msg from %s truncated (n=%d, limit %d)\n", 
						   sender, n, bufsz);
		*(channel->channelbuf + bufsz - 5) = '\0';
	}
	else {
		switch(channel->channelid) {
		  case C_STATUS:
			n = snprintf(channel->channelbuf, (bufsz-5),
				"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%s|%d", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, log->origin->name, hostname, log->test->testname, 
				(int) log->validtime, colnames[log->color], (log->testflags ? log->testflags : ""),
				colnames[log->oldcolor], (int) log->lastchange); 
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d|%s",
					(int)log->acktime, nlencode(log->ackmsg));
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d|%s",
					(int)log->enabletime, nlencode(log->dismsg));
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "\n%s", msg);
			}
			if (n > (bufsz-5)) {
				errprintf("Oversize status msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, log->test->testname, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_STACHG:
			n = snprintf(channel->channelbuf, (bufsz-5),
				"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%d", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, log->origin->name, hostname, log->test->testname, 
				(int) log->validtime, colnames[log->color], 
				colnames[log->oldcolor], (int) log->lastchange);
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d|%s",
					(int)log->enabletime, nlencode(log->dismsg));
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "\n%s", msg);
			}
			if (n > (bufsz-5)) {
				errprintf("Oversize stachg msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, log->test->testname, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_PAGE:
			if (strcmp(channelmarker, "ack") == 0) {
				n = snprintf(channel->channelbuf, (bufsz-5),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d\n%s", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					log->test->testname, log->host->ip,
					(int) log->acktime, msg);
			}
			else if (strcmp(channelmarker, "notify") == 0) {
				namelist_t *hi = hostinfo(hostname);

				n = snprintf(channel->channelbuf, (bufsz-5),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s\n%s", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					(log->test ? log->test->testname : ""), 
					(hi ? hi->page->pagepath : ""), 
					msg);
			}
			else {
				namelist_t *hi = hostinfo(hostname);

				n = snprintf(channel->channelbuf, (bufsz-5),
					"@@%s#%u|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%d|%s|%d\n%s", 
					channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
					sender, hostname, 
					log->test->testname, log->host->ip, (int) log->validtime, 
					colnames[log->color], colnames[log->oldcolor], (int) log->lastchange,
					(hi ? hi->page->pagepath : ""), 
					log->cookie, msg);
			}
			if (n > (bufsz-5)) {
				errprintf("Oversize page/ack/notify msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, (log->test ? log->test->testname : "<none>"), n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_DATA:
		  case C_CLIENT:
			/* Data channel messages are pre-formatted so we never go here */
			break;

		  case C_NOTES:
			n = snprintf(channel->channelbuf,  (bufsz-5),
				"@@%s#%u|%d.%06d|%s|%s\n%s", 
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int) tstamp.tv_usec, 
				sender, hostname, msg);
			if (n > (bufsz-5)) {
				errprintf("Oversize notes msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, log->test->testname, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_ENADIS:
			n = snprintf(channel->channelbuf, (bufsz-5),
				"@@%s#%u|%d.%06d|%s|%s|%s|%d",
				channelmarker, channel->seq, (int) tstamp.tv_sec, (int)tstamp.tv_usec,
				sender, hostname, log->test->testname, (int) log->enabletime);
			if (n > (bufsz-5)) {
				errprintf("Oversize enadis msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, log->test->testname, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_LAST:
			break;
		}
	}
	/* Terminate the message */
	strncat(channel->channelbuf, "\n@@\n", (bufsz-1));

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

	dprintf("<- posttochannel\n");

	return;
}


void log_ghost(char *hostname, char *sender, char *msg)
{
	ghostlist_t *gwalk;

	dprintf("-> log_ghost\n");

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

	dprintf("<- log_ghost\n");
}

hobbitd_log_t *find_log(char *hostname, char *testname, char *origin, hobbitd_hostlist_t **host)
{
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk;
	hobbitd_testlist_t *twalk;
	htnames_t *owalk = NULL;
	hobbitd_log_t *lwalk;

	*host = NULL;

	hosthandle = rbtFind(rbhosts, hostname);
	if (hosthandle != rbtEnd(rbhosts)) *host = hwalk = gettreeitem(rbhosts, hosthandle); else return NULL;

	if (origin) for (owalk = origins; (owalk && strcasecmp(origin, owalk->name)); owalk = owalk->next);

	for (twalk = tests; (twalk && strcasecmp(testname, twalk->testname)); twalk = twalk->next);
	if (twalk == NULL) return NULL;

	for (lwalk = hwalk->logs; (lwalk && ((lwalk->test != twalk) || (lwalk->origin != owalk))); lwalk = lwalk->next);
	return lwalk;
}

void get_hts(char *msg, char *sender, char *origin,
	     hobbitd_hostlist_t **host, hobbitd_testlist_t **test, hobbitd_log_t **log, 
	     int *color, char **downcause, int *alltests, int createhost, int createlog)
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
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk = NULL;
	hobbitd_testlist_t *twalk = NULL;
	htnames_t *owalk = NULL;
	hobbitd_log_t *lwalk = NULL;

	dprintf("-> get_hts\n");

	MEMDEFINE(hostip);
	*hostip = '\0';

	*host = NULL;
	*test = NULL;
	*log = NULL;
	*color = -1;
	if (downcause) *downcause = NULL;
	if (alltests) *alltests = 0;

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

		knownname = knownhost(hostname, hostip, ghosthandling);
		if (knownname == NULL) {
			log_ghost(hostname, sender, msg);
			goto done;
		}
		hostname = knownname;
	}

	hosthandle = rbtFind(rbhosts, hostname);
	if (hosthandle == rbtEnd(rbhosts)) hwalk = NULL;
	else hwalk = gettreeitem(rbhosts, hosthandle);

	if (createhost && (hosthandle == rbtEnd(rbhosts))) {
		hwalk = (hobbitd_hostlist_t *)malloc(sizeof(hobbitd_hostlist_t));
		hwalk->hostname = strdup(hostname);
		strcpy(hwalk->ip, hostip);
		hwalk->logs = NULL;
		hwalk->clientmsg = NULL;
		if (rbtInsert(rbhosts, hwalk->hostname, hwalk)) {
			errprintf("Insert into rbhosts failed\n");
		}
		hostcount++;
	}

	if (testname && *testname) {
		if (alltests && (*testname == '*')) {
			*alltests = 1;
			return;
		}

		for (twalk = tests; (twalk && strcasecmp(testname, twalk->testname)); twalk = twalk->next);
		if (createlog && (twalk == NULL)) {
			twalk = (hobbitd_testlist_t *)malloc(sizeof(hobbitd_testlist_t));
			twalk->testname = strdup(testname);
			twalk->next = tests;
			tests = twalk;
		}
	}
	else {
		if (createlog) errprintf("Bogus message from %s: No testname '%s'\n", sender, msg);
	}

	if (origin) {
		for (owalk = origins; (owalk && strcasecmp(origin, owalk->name)); owalk = owalk->next);
		if (createlog && (owalk == NULL)) {
			owalk = (htnames_t *)malloc(sizeof(htnames_t));
			owalk->name = strdup(origin);
			origins = owalk;
		}
	}
	if (hwalk && twalk && owalk) {
		for (lwalk = hwalk->logs; (lwalk && ((lwalk->test != twalk) || (lwalk->origin != owalk))); lwalk = lwalk->next);
		if (createlog && (lwalk == NULL)) {
			lwalk = (hobbitd_log_t *)malloc(sizeof(hobbitd_log_t));
			lwalk->color = lwalk->oldcolor = NO_COLOR;
			lwalk->activealert = 0;
			lwalk->histsynced = 0;
			lwalk->testflags = NULL;
			lwalk->sender[0] = '\0';
			lwalk->host = hwalk;
			lwalk->test = twalk;
			lwalk->origin = owalk;
			lwalk->message = NULL;
			lwalk->msgsz = 0;
			lwalk->dismsg = lwalk->ackmsg = NULL;
			lwalk->acklist = NULL;
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
		if ((*color == COL_RED) || (*color == COL_YELLOW)) {
			char *cause;

			cause = check_downtime(hostname, testname);
			if (cause) *color = COL_BLUE;
			if (downcause) *downcause = cause;
		}
	}
	xfree(firstline);

	MEMUNDEFINE(hostip);

	dprintf("<- get_hts\n");
}


hobbitd_log_t *find_cookie(int cookie)
{
	/*
	 * Find a cookie we have issued.
	 */
	hobbitd_log_t *result = NULL;
	RbtIterator hosthandle;
	hobbitd_log_t *lwalk = NULL;
	int found = 0;

	dprintf("-> find_cookie\n");

	for (hosthandle = rbtBegin(rbhosts); ((hosthandle != rbtEnd(rbhosts)) && !found); hosthandle = rbtNext(rbhosts, hosthandle)) {
		hobbitd_hostlist_t *hwalk;

		hwalk = gettreeitem(rbhosts, hosthandle);
		for (lwalk = hwalk->logs; (lwalk && (lwalk->cookie != cookie)); lwalk = lwalk->next);
		found = (lwalk != NULL);
	}

	if (found && lwalk && (lwalk->cookieexpires > time(NULL))) {
		result = lwalk;
	}

	dprintf("<- find_cookie\n");

	return result;
}


void handle_status(unsigned char *msg, char *sender, char *hostname, char *testname, hobbitd_log_t *log, int newcolor, char *downcause)
{
	int validity = 30;	/* validity is counted in minutes */
	time_t now = time(NULL);
	int msglen, issummary;
	enum alertstate_t oldalertstatus, newalertstatus;

	dprintf("->handle_status\n");

	if (msg == NULL) {
		errprintf("handle_status got a NULL message for %s.%s, sender %s\n", 
			  textornull(hostname), textornull(testname), textornull(sender));
		return;
	}

	msglen = strlen(msg);
	if (msglen == 0) {
		errprintf("Bogus status message contains no data: Sent from %s\n", sender);
		return;
	}
	if (msg_data(msg) == (char *)msg) {
		errprintf("Bogus status message: msg_data finds no host.test. Sent from: '%s', data:'%s'\n",
			  sender, msg);
		return;
	}

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
	else {
		/* If we got a downcause, and the status is not disabled, use downcause as the disable text */
		if (log->dismsg) { xfree(log->dismsg); log->dismsg = NULL; }
		if (downcause && (newcolor == COL_BLUE)) log->dismsg = strdup(downcause);
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

	if (log->acklist) {
		ackinfo_t *awalk;

		if ((oldalertstatus != A_OK) && (newalertstatus == A_OK)) {
			/* The status recovered. Set the "clearack" timer */
			time_t cleartime = getcurrenttime(NULL) + ACKCLEARDELAY;
			for (awalk = log->acklist; (awalk); awalk = awalk->next) awalk->cleartime = cleartime;
		}
		else if ((oldalertstatus == A_OK) && (newalertstatus != A_OK)) {
			/* The status went into a failure-mode. Any acks are revived */
			for (awalk = log->acklist; (awalk); awalk = awalk->next) awalk->cleartime = awalk->validuntil;
		}
	}

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
		p += strlen(colorname(newcolor));

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

	if (!issummary && (!log->histsynced || (log->oldcolor != newcolor))) {
		dprintf("oldcolor=%d, oldas=%d, newcolor=%d, newas=%d\n", 
			log->oldcolor, oldalertstatus, newcolor, newalertstatus);

		/*
		 * Change of color always goes to the status-change channel.
		 */
		dprintf("posting to stachg channel\n");
		posttochannel(stachgchn, channelnames[C_STACHG], msg, sender, hostname, log, NULL);
		log->lastchange = time(NULL);
		log->histsynced = 1;
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

	dprintf("<-handle_status\n");
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

	dprintf("-> handle_meta\n");

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

	dprintf("<- handle_meta\n");
}

void handle_data(char *msg, char *sender, char *origin, char *hostname, char *testname)
{
	char *chnbuf;
	int buflen = 0;

	dprintf("->handle_data\n");

	if (origin) buflen += strlen(origin); else dprintf("   origin is NULL\n");
	if (hostname) buflen += strlen(hostname); else dprintf("  hostname is NULL\n");
	if (testname) buflen += strlen(testname); else dprintf("  testname is NULL\n");
	if (msg) buflen += strlen(msg); else dprintf("  msg is NULL\n");
	buflen += 4;

	chnbuf = (char *)malloc(buflen);
	snprintf(chnbuf, buflen, "%s|%s|%s\n%s", 
		 (origin ? origin : ""), 
		 (hostname ? hostname : ""), 
		 (testname ? testname : ""), 
		 msg);

	posttochannel(datachn, channelnames[C_DATA], msg, sender, hostname, NULL, chnbuf);
	xfree(chnbuf);
	dprintf("<-handle_data\n");
}

void handle_notes(char *msg, char *sender, char *hostname)
{
	dprintf("->handle_notes\n");
	posttochannel(noteschn, channelnames[C_NOTES], msg, sender, hostname, NULL, NULL);
	dprintf("<-handle_notes\n");
}

void handle_enadis(int enabled, char *msg, char *sender)
{
	char *firstline = NULL, *hosttest = NULL, *durstr = NULL, *txtstart = NULL;
	char *hname = NULL, *tname = NULL;
	int duration = 0;
	int alltests = 0;
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk = NULL;
	hobbitd_testlist_t *twalk = NULL;
	hobbitd_log_t *log;
	char *p;
	char hostip[20];

	dprintf("->handle_enadis\n");

	MEMDEFINE(hostip);

	p = strchr(msg, '\n'); if (p) *p = '\0';
	firstline = strdup(msg);
	if (p) *p = '\n';

	p = strtok(firstline, " \t");
	if (p) hosttest = strtok(NULL, " \t");
	if (hosttest) durstr = strtok(NULL, " \t");
	if (!hosttest) {
		errprintf("Invalid enable/disable from %s - no host/test specified\n", sender);
		goto done;
	}

	if (!enabled) {
		if (durstr) {
			duration = durationvalue(durstr);
			txtstart = msg + (durstr + strlen(durstr) - firstline);
			txtstart += strspn(txtstart, " \t\r\n");
			if (*txtstart == '\0') txtstart = "(No reason given)";
		}
		else {
			errprintf("Invalid disable from %s - no duration specified\n", sender);
			goto done;
		}
	}

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
	hname = knownhost(hosttest, hostip, ghosthandling);
	if (hname == NULL) goto done;

	hosthandle = rbtFind(rbhosts, hname);
	if (hosthandle == rbtEnd(rbhosts)) {
		/* Unknown host */
		goto done;
	}
	else hwalk = gettreeitem(rbhosts, hosthandle);

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

		if (alltests) {
			for (log = hwalk->logs; (log); log = log->next) {
				log->enabletime = log->validtime = expires;
				if (txtstart) {
					if (log->dismsg) xfree(log->dismsg);
					log->dismsg = strdup(txtstart);
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);
				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->testname, log, COL_BLUE, NULL);
			}
		}
		else {
			for (log = hwalk->logs; (log && (log->test != twalk)); log = log->next) ;
			if (log) {
				log->enabletime = log->validtime = expires;
				if (txtstart) {
					if (log->dismsg) xfree(log->dismsg);
					log->dismsg = strdup(txtstart);
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);

				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->testname, log, COL_BLUE, NULL);
			}
		}

	}

done:
	MEMUNDEFINE(hostip);
	xfree(firstline);

	dprintf("<-handle_enadis\n");

	return;
}


void handle_ack(char *msg, char *sender, hobbitd_log_t *log, int duration)
{
	char *p;

	dprintf("->handle_ack\n");

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

	dprintf("<-handle_ack\n");
	return;
}

void handle_ackinfo(char *msg, char *sender, hobbitd_log_t *log)
{
	int level = -1;
	time_t validuntil = -1, itemval;
	time_t received = getcurrenttime(NULL);
	char *ackedby = NULL, *ackmsg = NULL;
	char *tok, *item;
	int itemno = 0;

	debug = 1;

	tok = msg;
	while (tok) {
		tok += strspn(tok, " \t\n");
		item = tok; itemno++;
		tok = strchr(tok, '\n'); if (tok) { *tok = '\0'; tok++; }

		switch (itemno) {
		  case 1: break; /* First line has just the HOST.TEST */
		  case 2: level = atoi(item); break;
		  case 3: itemval = atoi(item); 
			  if (itemval == -1) itemval = 365*24*60*60; /* 1 year */
			  validuntil = received + itemval; 
			  break;
		  case 4: ackedby = strdup(item); break;
		  case 5: ackmsg = strdup(item); break;
		}
	}

	if ((level >= 0) && (validuntil > received) && ackedby && ackmsg) {
		ackinfo_t *newack;
		int isnew;

		dprintf("Got ackinfo: Level=%d,until=%d,ackby=%s,msg=%s\n", level, validuntil, ackedby, ackmsg);

		/* See if we already have this ack in the list */
		for (newack = log->acklist; (newack && ((level != newack->level) || strcmp(newack->ackedby, ackedby))); newack = newack->next);

		isnew = (newack == NULL);
		dprintf("This ackinfo is %s\n", (isnew ? "new" : "old"));
		if (isnew) {
			dprintf("Creating new ackinfo record\n");
			newack = (ackinfo_t *)malloc(sizeof(ackinfo_t));
		}
		else {
			/* Drop the old data so we dont leak memory */
			dprintf("Dropping old ackinfo data: From %s, msg=%s\n", newack->ackedby, newack->msg);
			if (newack->ackedby) xfree(newack->ackedby); 
			if (newack->msg) xfree(newack->msg);
		}

		newack->level = level;
		newack->received = received;
		newack->validuntil = newack->cleartime = validuntil;
		newack->ackedby = ackedby;
		newack->msg = ackmsg;

		if (isnew) {
			newack->next = log->acklist;
			log->acklist = newack;
		}
	}
	else {
		if (ackedby) xfree(ackedby);
		if (ackmsg) xfree(ackmsg);
	}

	debug = 0;
}

void handle_notify(char *msg, char *sender, hobbitd_log_t *log)
{
	char *msgtext;

	dprintf("-> handle_notify\n");

	msgtext = msg_data(msg);

	/* Tell the pagers */
	posttochannel(pagechn, "notify", msgtext, sender, log->host->hostname, log, NULL);

	dprintf("<- handle_notify\n");
	return;
}

void handle_client(char *msg, char *sender, char *hostname, char *clienttype)
{
	char *chnbuf;
	int msglen, buflen = 0;
	RbtIterator hosthandle;

	dprintf("->handle_client\n");

	if (hostname) buflen += strlen(hostname); else dprintf("  hostname is NULL\n");
	if (clienttype) buflen += strlen(clienttype); else dprintf("  clienttype is NULL\n");
	if (msg) { msglen = strlen(msg); buflen += msglen; } else { dprintf("  msg is NULL\n"); return; }
	buflen += 4;

	if (save_clientlogs) {
		hosthandle = rbtFind(rbhosts, hostname);
		if (hosthandle != rbtEnd(rbhosts)) {
			hobbitd_hostlist_t *hwalk;
			hwalk = gettreeitem(rbhosts, hosthandle);

			if (hwalk->clientmsg) {
				if (strlen(hwalk->clientmsg) >= msglen)
					strcpy(hwalk->clientmsg, msg);
				else {
					xfree(hwalk->clientmsg);
					hwalk->clientmsg = strdup(msg);
				}
			}
			else {
				hwalk->clientmsg = strdup(msg);
			}
		}
	}

	chnbuf = (char *)malloc(buflen);
	snprintf(chnbuf, buflen, "%s|%s\n%s", 
		 (hostname ? hostname : ""), 
		 (clienttype ? clienttype : ""), 
		 msg);

	posttochannel(clientchn, channelnames[C_CLIENT], msg, sender, hostname, NULL, chnbuf);
	xfree(chnbuf);
	dprintf("<-handle_client\n");
}



void flush_acklist(hobbitd_log_t *zombie, int flushall)
{
	ackinfo_t *awalk, *newhead = NULL, *newtail = NULL;
	time_t now = getcurrenttime(NULL);

	awalk = zombie->acklist;
	while (awalk) {
		ackinfo_t *tmp = awalk;
		awalk = awalk->next;

		if (flushall || (tmp->cleartime < now) || (tmp->validuntil < now)) {
			if (tmp->ackedby) xfree(tmp->ackedby);
			if (tmp->msg) xfree(tmp->msg);
			xfree(tmp);
		}
		else {
			/* We have a record we want to keep */
			if (newhead == NULL) {
				newhead = newtail = tmp;
			}
			else {
				newtail->next = tmp;
				newtail = tmp;
			}
		}
	}

	if (newtail) newtail->next = NULL;
	zombie->acklist = newhead;
}

char *acklist_string(hobbitd_log_t *log, int level)
{
	static char *res = NULL;
	static int ressz = 0;
	ackinfo_t *awalk;
	char tmpstr[512];

	if (log->acklist == NULL) return NULL;

	if (res) *res = '\0';
	for (awalk = log->acklist; (awalk); awalk = awalk->next) {
		if ((level != -1) && (awalk->level != level)) continue;
		snprintf(tmpstr, sizeof(tmpstr), "%d:%d:%d:%s:%s\n", 
			 (int)awalk->received, (int)awalk->validuntil, 
			 awalk->level, awalk->ackedby, awalk->msg);
		tmpstr[sizeof(tmpstr)-1] = '\0';
		addtobuffer(&res, &ressz, tmpstr);
	}

	return res;
}

void free_log_t(hobbitd_log_t *zombie)
{
	hobbitd_meta_t *mwalk, *mtmp;

	dprintf("-> free_log_t\n");
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
	flush_acklist(zombie, 1);
	xfree(zombie);
	dprintf("<- free_log_t\n");
}

void handle_dropnrename(enum droprencmd_t cmd, char *sender, char *hostname, char *n1, char *n2)
{
	char hostip[20];
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk;
	hobbitd_testlist_t *twalk, *newt;
	hobbitd_log_t *lwalk;
	char *marker = NULL;
	char *canonhostname;

	dprintf("-> handle_dropnrename\n");
	MEMDEFINE(hostip);

	{
		/*
		 * We pass drop- and rename-messages to the workers, whether 
		 * we know about this host or not. It could be that the drop command
		 * arrived after we had already re-loaded the bb-hosts file, and 
		 * so the host is no longer known by us - but there is still some
		 * data stored about it that needs to be cleaned up.
		 */

		char *msgbuf = (char *)malloc(20 + strlen(hostname) + (n1 ? strlen(n1) : 0) + (n2 ? strlen(n2) : 0));

		*msgbuf = '\0';
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
			posttochannel(clientchn, marker, NULL, sender, NULL, NULL, msgbuf);
		}

		xfree(msgbuf);
	}


	/*
	 * Now clean up our internal state info, if there is any.
	 * NB: knownhost() may return NULL, if the bb-hosts file was re-loaded before
	 * we got around to cleaning up a host.
	 */
	canonhostname = knownhost(hostname, hostip, ghosthandling);
	if (canonhostname) hostname = canonhostname;

	hosthandle = rbtFind(rbhosts, hostname);
	if (hosthandle == rbtEnd(rbhosts)) goto done;
	else hwalk = gettreeitem(rbhosts, hosthandle);

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
		rbtErase(rbhosts, hosthandle);
		hostcount--;

		/* Loop through the host logs and free them */
		lwalk = hwalk->logs;
		while (lwalk) {
			hobbitd_log_t *tmp = lwalk;
			lwalk = lwalk->next;

			free_log_t(tmp);
		}

		/* Free the hostlist entry */
		xfree(hwalk->hostname);
		if (hwalk->clientmsg) xfree(hwalk->clientmsg);
		xfree(hwalk);
		break;

	  case CMD_RENAMEHOST:
		rbtErase(rbhosts, hosthandle);
		if (strlen(hwalk->hostname) <= strlen(n1)) {
			strcpy(hwalk->hostname, n1);
		}
		else {
			xfree(hwalk->hostname);
			hwalk->hostname = strdup(n1);
		}
		rbtInsert(rbhosts, hwalk->hostname, hwalk);
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

	dprintf("<- handle_dropnrename\n");

	return;
}


int get_config(char *fn, conn_t *msg)
{
	char fullfn[PATH_MAX];
	FILE *fd = NULL;
	int done = 0;
	int n;
	char *inbuf = NULL;
	int inbufsz;

	dprintf("-> get_config %s\n", fn);
	sprintf(fullfn, "%s/etc/%s", xgetenv("BBHOME"), fn);
	fd = stackfopen(fullfn, "r");
	if (fd == NULL) {
		errprintf("Config file %s not found\n", fn);
		return -1;
	}

	*msg->buf = '\0';
	msg->bufp = msg->buf;
	msg->buflen = 0;
	do {
		done = (stackfgets(&inbuf, &inbufsz, "include", NULL) == NULL);
		if (!done) {
			addtobuffer(&msg->buf, &msg->bufsz, inbuf);
			n = strlen(inbuf);
			msg->buflen += n;
			msg->bufp += n;
		}
	} while (!done);

	stackfclose(fd);
	if (inbuf) xfree(inbuf);

	dprintf("<- get_config\n");

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

void setup_filter(char *buf, char *defaultfields, char **spage, char **shost, char **stest, int *scolor, int *acklevel, char **fields)
{
	char *tok, *s;
	int idx = 0;

	dprintf("-> setup_filter: %s\n", buf);

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
		else if (strncmp(tok, "color=", 6) == 0) *scolor = colorset(tok+6, 0);
		else if (strncmp(tok, "acklevel=", 9) == 0) *acklevel = atoi(tok+9);
		else {
			/* Might be an old-style HOST.TEST request */
			char *hname, *tname, hostip[20];

			MEMDEFINE(hostip);

			hname = tok;
			tname = strrchr(tok, '.');
			if (tname) { *tname = '\0'; tname++; }
			s = hname; while ((s = strchr(s, ',')) != NULL) *s = '.';

			*shost = knownhost(hname, hostip, ghosthandling);
			*stest = tname;
		}

		tok = strtok(NULL, " \t\r\n");
	}

	/* If no fields given, provide the default set. */
	if (*fields == NULL) *fields = defaultfields;

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

	dprintf("<- setup_filter: %s\n", buf);
}

void generate_outbuf(char **outbuf, char **outpos, int *outsz, 
		     hobbitd_hostlist_t *hwalk, hobbitd_log_t *lwalk, int acklevel)
{
	int f_idx;
	char *buf, *bufp;
	int bufsz;
	char *eoln;

	buf = *outbuf;
	bufp = *outpos;
	bufsz = *outsz;

	for (f_idx = 0; (boardfields[f_idx] != F_NONE); f_idx++) {
		int needed = 1024;
		int used = (bufp - buf);
		char *acklist = NULL;

		switch (boardfields[f_idx]) {
		  case F_ACKMSG: if (lwalk->ackmsg) needed += 2*strlen(lwalk->ackmsg); break;
		  case F_DISMSG: if (lwalk->dismsg) needed += 2*strlen(lwalk->dismsg); break;
		  case F_MSG: needed += 2*strlen(lwalk->message); break;

		  case F_ACKLIST:
			flush_acklist(lwalk, 0);
			acklist = acklist_string(lwalk, acklevel);
			if (acklist) needed += 2*strlen(acklist);
			break;

		  default: break;
		}

		if ((bufsz - used) < needed) {
			bufsz += 4096 + needed;
			buf = (char *)realloc(buf, bufsz);
			bufp = buf + used;
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
		  case F_CLIENT: bufp += sprintf(bufp, "%s", (hwalk->clientmsg ? "Y" : "N")); break;
		  case F_ACKLIST: if (acklist) bufp += sprintf(bufp, "%s", nlencode(acklist)); break;
		  case F_LAST: break;
		}
	}
	bufp += sprintf(bufp, "\n");

	*outbuf = buf;
	*outpos = bufp;
	*outsz = bufsz;
}


void do_message(conn_t *msg, char *origin)
{
	static int nesting = 0;
	hobbitd_hostlist_t *h;
	hobbitd_testlist_t *t;
	hobbitd_log_t *log;
	int color;
	char *downcause;
	char sender[20];
	time_t now;
	char *msgfrom;

	nesting++;
	if (debug) {
		char *eoln = strchr(msg->buf, '\n');

		if (eoln) *eoln = '\0';
		dprintf("-> do_message/%d (%d bytes): %s\n", nesting, msg->buflen, msg->buf);
		if (eoln) *eoln = '\n';
	}

	MEMDEFINE(sender);

	/* Most likely, we will not send a response */
	msg->doingwhat = NOTALK;
	strncpy(sender, inet_ntoa(msg->addr.sin_addr), sizeof(sender));
	now = time(NULL);

	if (traceall || tracelist) {
		int found = 0;

		if (traceall) {
			found = 1;
		}
		else {
			int i = 0;
			do {
				if ((tracelist[i].ipval & tracelist[i].ipmask) == (ntohl(msg->addr.sin_addr.s_addr) & tracelist[i].ipmask)) {
					found = 1;
				}
				i++;
			} while (!found && (tracelist[i].ipval != 0));
		}

		if (found) {
			char tracefn[PATH_MAX];
			struct timeval tv;
			struct timezone tz;
			FILE *fd;

			gettimeofday(&tv, &tz);

			sprintf(tracefn, "%s/%d_%06d_%s.trace", xgetenv("BBTMP"), 
				(int) tv.tv_sec, (int) tv.tv_usec, sender);
			fd = fopen(tracefn, "w");
			if (fd) {
				fwrite(msg->buf, msg->buflen, 1, fd);
				fclose(fd);
			}

			if (ignoretraced) goto done;
		}
	}

	/* Count statistics */
	update_statistics(msg->buf);

	if (strncmp(msg->buf, "combo\n", 6) == 0) {
		char *currmsg, *nextmsg;

		currmsg = msg->buf+6;
		do {
			int validsender = 1;

			nextmsg = strstr(currmsg, "\n\nstatus");
			if (nextmsg) { *(nextmsg+1) = '\0'; nextmsg += 2; }

			/* Pick out the real sender of this message */
			msgfrom = strstr(currmsg, "\nStatus message received from ");
			if (msgfrom) {
				sscanf(msgfrom, "\nStatus message received from %16s\n", sender);
				*msgfrom = '\0';
			}

			if (statussenders) {
				get_hts(currmsg, sender, origin, &h, &t, &log, &color, &downcause, NULL, 0, 0);
				if (!oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, currmsg)) validsender = 0;
			}

			if (validsender) {
				get_hts(currmsg, sender, origin, &h, &t, &log, &color, &downcause, NULL, 1, 1);
				if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
					fprintf(dbgfd, "\n---- combo message from %s ----\n%s---- end message ----\n", sender, currmsg);
					fflush(dbgfd);
				}

				if (color == COL_PURPLE) {
					errprintf("Ignored PURPLE status update from %s for %s.%s\n",
						  sender, (h ? h->hostname : "<unknown>"), (t ? t->testname : "unknown"));
				}
				else {
					/* Count individual status-messages also */
					update_statistics(currmsg);

					if (h && t && log && (color != -1) && (color != COL_PURPLE)) {
						handle_status(currmsg, sender, h->hostname, t->testname, log, color, downcause);
					}
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

			get_hts(currmsg, sender, origin, &h, &t, &log, &color, NULL, NULL, 0, 0);
			if (h && t && log && oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, currmsg)) {
				handle_meta(currmsg, log);
			}

			currmsg = nextmsg;
		} while (currmsg);
	}
	else if (strncmp(msg->buf, "status", 6) == 0) {
		msgfrom = strstr(msg->buf, "\nStatus message received from ");
		if (msgfrom) {
			sscanf(msgfrom, "\nStatus message received from %16s\n", sender);
			*msgfrom = '\0';
		}

		if (statussenders) {
			get_hts(msg->buf, sender, origin, &h, &t, &log, &color, &downcause, NULL, 0, 0);
			if (!oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, msg->buf)) goto done;
		}

		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, &downcause, NULL, 1, 1);
		if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
			fprintf(dbgfd, "\n---- status message from %s ----\n%s---- end message ----\n", sender, msg->buf);
			fflush(dbgfd);
		}

		if (color == COL_PURPLE) {
			errprintf("Ignored PURPLE status update from %s for %s.%s\n",
				  sender, (h ? h->hostname : "<unknown>"), (t ? t->testname : "unknown"));
		}
		else {
			if (h && t && log && (color != -1)) {
				handle_status(msg->buf, sender, h->hostname, t->testname, log, color, downcause);
			}
		}
	}
	else if (strncmp(msg->buf, "data", 4) == 0) {
		char *hostname = NULL, *testname = NULL;
		char *bhost, *ehost, *btest;
		char savechar;

		msgfrom = strstr(msg->buf, "\nStatus message received from ");
		if (msgfrom) {
			sscanf(msgfrom, "\nStatus message received from %16s\n", sender);
			*msgfrom = '\0';
		}

		bhost = msg->buf + strlen("data"); bhost += strspn(bhost, " \t");
		ehost = bhost + strcspn(bhost, " \t\r\n");
		savechar = *ehost; *ehost = '\0';

		btest = strrchr(bhost, '.');
		if (btest) {
			char *p;

			*btest = '\0';
			hostname = strdup(bhost);
			p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';
			*btest = '.';
			testname = strdup(btest+1);

			if (*hostname == '\0') { errprintf("Invalid data message from %s - blank hostname\n", sender); xfree(hostname); hostname = NULL; }
			if (*testname == '\0') { errprintf("Invalid data message from %s - blank testname\n", sender); xfree(testname); testname = NULL; }
		}
		else {
			errprintf("Invalid data message - no testname in '%s'\n", bhost);
		}

		*ehost = savechar;

		if (hostname && testname) {
			char *hname, hostip[20];

			MEMDEFINE(hostip);

			hname = knownhost(hostname, hostip, ghosthandling);

			if (hname == NULL) {
				log_ghost(hostname, sender, msg->buf);
			}
			else if (!oksender(statussenders, hostip, msg->addr.sin_addr, msg->buf)) {
				/* Invalid sender */
				errprintf("Invalid data message - sender %s not allowed for host %s\n", sender, hostname);
			}
			else {
				handle_data(msg->buf, sender, origin, hname, testname);
			}

			xfree(hostname); xfree(testname);

			MEMUNDEFINE(hostip);
		}
	}
	else if (strncmp(msg->buf, "summary", 7) == 0) {
		/* Summaries are always allowed. Or should we ? */
		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, NULL, NULL, 1, 1);
		if (h && t && log && (color != -1)) {
			handle_status(msg->buf, sender, h->hostname, t->testname, log, color, NULL);
		}
	}
	else if (strncmp(msg->buf, "notes", 5) == 0) {
		char *hostname, *bhost, *ehost, *p;
		char savechar;

		bhost = msg->buf + strlen("notes"); bhost += strspn(bhost, " \t");
		ehost = bhost + strcspn(bhost, " \t\r\n");
		savechar = *ehost; *ehost = '\0';
		hostname = strdup(bhost);
		*ehost = savechar;

		p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';
		if (*hostname == '\0') { errprintf("Invalid notes message from %s - blank hostname\n", sender); xfree(hostname); hostname = NULL; }

		if (hostname) {
			char *hname, hostip[20];

			MEMDEFINE(hostip);

			hname = knownhost(hostname, hostip, ghosthandling);
			if (hname == NULL) {
				log_ghost(hostname, sender, msg->buf);
			}
			else if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) {
				/* Invalid sender */
				errprintf("Invalid notes message - sender %s not allowed for host %s\n", sender, hostname);
			}
			else {
				handle_notes(msg->buf, sender, hostname);
			}

			xfree(hostname);

			MEMUNDEFINE(hostip);
		}
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
		char *conffn, *p;

		if (!oksender(statussenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		p = msg->buf + 6; p += strspn(p, " \t");
		conffn = strtok(p, " \t\r\n");
		if (conffn && (strstr("../", conffn) == NULL) && (get_config(conffn, msg) == 0) ) {
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf;
		}
	}
	else if (strncmp(msg->buf, "query ", 6) == 0) {
		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, NULL, NULL, 0, 0);
		if (!oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, msg->buf)) goto done;

		if (log) {
			xfree(msg->buf);
			msg->doingwhat = RESPONDING;
			if (log->message) {
				unsigned char *bol, *eoln;
				int msgcol;
				char response[500];

				bol = msg_data(log->message);
				msgcol = parse_color(bol);
				if (msgcol != -1) {
					/* Skip the color - it may be different in real life */
					bol += strlen(colorname(msgcol));
					bol += strspn(bol, " \t");
				}
				eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
				snprintf(response, sizeof(response), "%s %s\n", colorname(log->color), bol);
				response[sizeof(response)-1] = '\0';
				if (eoln) *eoln = '\n';

				msg->buf = msg->bufp = strdup(response);
				msg->buflen = strlen(msg->buf);
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
		 * hobbitdlog HOST.TEST [fields=FIELDLIST]
		 *
		 */

		char *spage = NULL, *shost = NULL, *stest = NULL, *fields = NULL;
		int scolor = -1, acklevel = -1;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf, 
		 	     "hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,ackmsg,dismsg,client",
			     &spage, &shost, &stest, &scolor, &acklevel, &fields);

		log = find_log(shost, stest, "", &h);
		if (log) {
			char *buf, *bufp;
			int bufsz;

			flush_acklist(log, 0);
			if (log->message == NULL) {
				errprintf("%s.%s has a NULL message\n", log->host->hostname, log->test->testname);
				log->message = strdup("");
			}

			bufsz = 1024 + strlen(log->message);
			if (log->ackmsg) bufsz += 2*strlen(log->ackmsg);
			if (log->dismsg) bufsz += 2*strlen(log->dismsg);

			xfree(msg->buf);
			bufp = buf = (char *)malloc(bufsz);
			generate_outbuf(&buf, &bufp, &bufsz, h, log, acklevel);
			bufp += sprintf(bufp, "%s", msg_data(log->message));

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

		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, NULL, NULL, 0, 0);
		if (log) {
			char *buf, *bufp;
			int bufsz, buflen;
			hobbitd_meta_t *mwalk;

			flush_acklist(log, 0);
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
		 */
		RbtIterator hosthandle;
		hobbitd_hostlist_t *hwalk;
		hobbitd_log_t *lwalk, *firstlog;
		hobbitd_log_t infologrec, rrdlogrec;
		hobbitd_testlist_t infotestrec, rrdtestrec;
		char *buf, *bufp;
		int bufsz;
		char *spage = NULL, *shost = NULL, *stest = NULL, *fields = NULL;
		int scolor = -1, acklevel = -1;
		static unsigned int lastboardsize = 0;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf, 
			     "hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,line1",
			     &spage, &shost, &stest, &scolor, &acklevel, &fields);

		if (lastboardsize == 0) {
			/* A guesstimate - 8 tests per hosts, 1KB/test (only 1st line of msg) */
			bufsz = (hostcount+1)*8*1024; 
		}
		else {
			/* Add 10% to the last size we used */
			bufsz = lastboardsize + (lastboardsize / 10);
		}
		bufp = buf = (char *)malloc(bufsz);

		/* Setup fake log-records for the "info" and "trends" data. */
		infotestrec.testname = xgetenv("INFOCOLUMN");
		infotestrec.next = NULL;
		memset(&infologrec, 0, sizeof(infologrec));
		infologrec.test = &infotestrec;

		rrdtestrec.testname = getenv("TRENDSCOLUMN");
		if (!rrdtestrec.testname || (strlen(rrdtestrec.testname) == 0)) rrdtestrec.testname = getenv("LARRDCOLUMN");
		if (!rrdtestrec.testname || (strlen(rrdtestrec.testname) == 0)) rrdtestrec.testname = "trends";

		rrdtestrec.next = NULL;
		memset(&rrdlogrec, 0, sizeof(rrdlogrec));
		rrdlogrec.test = &rrdtestrec;

		infologrec.color = rrdlogrec.color = COL_GREEN;
		infologrec.message = rrdlogrec.message = "";

		for (hosthandle = rbtBegin(rbhosts); (hosthandle != rbtEnd(rbhosts)); hosthandle = rbtNext(rbhosts, hosthandle)) {
			hwalk = gettreeitem(rbhosts, hosthandle);
			if (!hwalk) {
				errprintf("host-tree has a record with no data\n");
				continue;
			}

			/* Hostname filter */
			if (shost && (strcmp(hwalk->hostname, shost) != 0)) continue;

			firstlog = hwalk->logs;

			if ((strcmp(hwalk->hostname, "summary") != 0) && (strcmp(hwalk->hostname, "dialup") != 0)) {
				namelist_t *hinfo = hostinfo(hwalk->hostname);

				if (!hinfo) {
					errprintf("Hostname '%s' in tree, but no host-info\n", hwalk->hostname);
					continue;
				}

				/* Host pagename filter */
				if (spage && (strncmp(hinfo->page->pagepath, spage, strlen(spage)) != 0)) continue;

				/* Handle NOINFO and NOTRENDS here */
				if (!bbh_item(hinfo, BBH_FLAG_NOINFO)) {
					infologrec.next = firstlog;
					firstlog = &infologrec;
				}
				if (!bbh_item(hinfo, BBH_FLAG_NOTRENDS)) {
					rrdlogrec.next = firstlog;
					firstlog = &rrdlogrec;
				}
			}

			for (lwalk = firstlog; (lwalk); lwalk = lwalk->next) {
				/* Testname filter */
				if (stest && (strcmp(lwalk->test->testname, stest) != 0)) continue;

				/* Color filter */
				if ((scolor != -1) && (((1 << lwalk->color) & scolor) == 0)) continue;

				if (lwalk->message == NULL) {
					errprintf("%s.%s has a NULL message\n", lwalk->host->hostname, lwalk->test->testname);
					lwalk->message = strdup("");
				}

				generate_outbuf(&buf, &bufp, &bufsz, hwalk, lwalk, acklevel);
			}
		}
		*bufp = '\0';

		xfree(msg->buf);
		msg->doingwhat = RESPONDING;
		msg->bufp = msg->buf = buf;
		msg->buflen = (bufp - buf);
		if (msg->buflen > lastboardsize) lastboardsize = msg->buflen;
	}
	else if (strncmp(msg->buf, "hobbitdxboard", 13) == 0) {
		/* 
		 * Request for a summmary of all known status logs in XML format
		 *
		 */
		RbtIterator hosthandle;
		hobbitd_hostlist_t *hwalk;
		hobbitd_log_t *lwalk;
		char *buf, *bufp;
		int bufsz;
		char *spage = NULL, *shost = NULL, *stest = NULL, *fields = NULL;
		int scolor = -1, acklevel = -1;
		static unsigned int lastboardsize = 0;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf,
			     "hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,line1",
			     &spage, &shost, &stest, &scolor, &acklevel, &fields);

		if (lastboardsize == 0) {
			/* A guesstimate - 8 tests per hosts, 2KB/test (only 1st line of msg) */
			bufsz = (hostcount+1)*8*2048; 
		}
		else {
			/* Add 10% to the last size we used */
			bufsz = lastboardsize + (lastboardsize / 10);
		}
		bufp = buf = (char *)malloc(bufsz);

		bufp += sprintf(bufp, "<?xml version='1.0' encoding='ISO-8859-1'?>\n");
		bufp += sprintf(bufp, "<StatusBoard>\n");

		for (hosthandle = rbtBegin(rbhosts); (hosthandle != rbtEnd(rbhosts)); hosthandle = rbtNext(rbhosts, hosthandle)) {
			hwalk = gettreeitem(rbhosts, hosthandle);

			/* Host pagename filter */
			if (spage) {
				namelist_t *hi = hostinfo(hwalk->hostname);
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
		if (msg->buflen > lastboardsize) lastboardsize = msg->buflen;
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
	else if (strncmp(msg->buf, "ackinfo ", 8) == 0) {
		/* ackinfo HOST.TEST\nlevel\nvaliduntil\nackedby\nmsg */
		int ackall = 0;

		if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, NULL, &ackall, 0, 0);
		if (log) {
			handle_ackinfo(msg->buf, sender, log);
		}
		else if (ackall) {
			hobbitd_log_t *lwalk;

			for (lwalk = h->logs; (lwalk); lwalk = lwalk->next) {
				if (decide_alertstate(lwalk->color) != A_OK) {
					handle_ackinfo(msg->buf, sender, lwalk);
				}
			}
		}
	}
	else if (strncmp(msg->buf, "drop ", 5) == 0) {
		char *hostname = NULL, *testname = NULL;
		char *p;

		if (!oksender(adminsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		p = msg->buf + 4; p += strspn(p, " \t");
		hostname = strtok(p, " \t");
		if (hostname) testname = strtok(NULL, " \t");

		if (hostname && !testname) {
			handle_dropnrename(CMD_DROPHOST, sender, hostname, NULL, NULL);
		}
		else if (hostname && testname) {
			handle_dropnrename(CMD_DROPTEST, sender, hostname, testname, NULL);
		}
	}
	else if (strncmp(msg->buf, "rename ", 7) == 0) {
		char *hostname = NULL, *n1 = NULL, *n2 = NULL;
		char *p;

		if (!oksender(adminsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		p = msg->buf + 6; p += strspn(p, " \t");
		hostname = strtok(p, " \t");
		if (hostname) n1 = strtok(NULL, " \t");
		if (n1) n2 = strtok(NULL, " \t");

		if (hostname && n1 && !n2) {
			/* Host rename */
			handle_dropnrename(CMD_RENAMEHOST, sender, hostname, n1, NULL);
		}
		else if (hostname && n1 && n2) {
			/* Test rename */
			handle_dropnrename(CMD_RENAMETEST, sender, hostname, n1, n2);
		}
	}
	else if (strncmp(msg->buf, "dummy", 5) == 0) {
		/* Do nothing */
	}
	else if (strncmp(msg->buf, "notify", 6) == 0) {
		if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;
		get_hts(msg->buf, sender, origin, &h, &t, &log, &color, NULL, NULL, 0, 0);
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
	else if (strncmp(msg->buf, "client ", 7) == 0) {
		char *hostname = NULL, *clienttype = NULL;
		char *bhost, *ehost, *btype;
		char savechar;

		msgfrom = strstr(msg->buf, "\nStatus message received from ");
		if (msgfrom) {
			sscanf(msgfrom, "\nStatus message received from %16s\n", sender);
			*msgfrom = '\0';
		}

		bhost = msg->buf + strlen("client"); bhost += strspn(bhost, " \t");
		ehost = bhost + strcspn(bhost, " \t\r\n");
		savechar = *ehost; *ehost = '\0';

		btype = strrchr(bhost, '.');
		if (btype) {
			char *p;

			*btype = '\0';
			hostname = strdup(bhost);
			p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';
			*btype = '.';
			clienttype = strdup(btype+1);

			if (*hostname == '\0') { errprintf("Invalid client message from %s - blank hostname\n", sender); xfree(hostname); hostname = NULL; }
			if (*clienttype == '\0') { errprintf("Invalid client message from %s - blank type\n", sender); xfree(clienttype); clienttype = NULL; }
		}
		else {
			errprintf("Invalid client message - no type in '%s'\n", bhost);
		}

		*ehost = savechar;

		if (hostname && clienttype) {
			char *hname, hostip[20];

			MEMDEFINE(hostip);

			hname = knownhost(hostname, hostip, ghosthandling);

			if (hname == NULL) {
				log_ghost(hostname, sender, msg->buf);
			}
			else if (!oksender(statussenders, hostip, msg->addr.sin_addr, msg->buf)) {
				/* Invalid sender */
				errprintf("Invalid client message - sender %s not allowed for host %s\n", sender, hostname);
			}
			else {
				handle_client(msg->buf, sender, hname, clienttype);
			}

			xfree(hostname); xfree(clienttype);

			MEMUNDEFINE(hostip);
		}
	}
	else if (strncmp(msg->buf, "clientlog ", 10) == 0) {
		char *hostname, *sect, *p;
		RbtIterator hosthandle;
		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		hostname = msg->buf + strlen("clientlog"); hostname += strspn(hostname, " ");
		sect = strstr(hostname, " section="); if (sect) { *sect = '\0'; sect += 9; }
		p = hostname + strcspn(hostname, " \t\r\n"); *p = '\0';
		if (sect) { p = sect + strcspn(sect, " \t\r\n"); *p = '\0'; }

		hosthandle = rbtFind(rbhosts, hostname);
		if (hosthandle != rbtEnd(rbhosts)) {
			hobbitd_hostlist_t *hwalk;
			hwalk = gettreeitem(rbhosts, hosthandle);

			if (hwalk->clientmsg) {
				if (sect) {
					char *sectmarker = (char *)malloc(strlen(sect) + 4);
					char *beginp, *endp;

					sprintf(sectmarker, "\n[%s]", sect);
					beginp = strstr(hwalk->clientmsg, sectmarker);
					if (beginp) {
						beginp += strlen(sectmarker);
						beginp += strspn(beginp, "\r\n\t ");

						endp = strstr(beginp, "\n[");
						if (endp) { endp++; *endp = '\0'; }

						msg->doingwhat = RESPONDING;
						xfree(msg->buf);
						msg->bufp = msg->buf = strdup(beginp);
						msg->buflen = strlen(msg->buf);

						if (endp) *endp = '[';
					}

					xfree(sectmarker);
				}
				else {
					msg->doingwhat = RESPONDING;
					xfree(msg->buf);
					msg->bufp = msg->buf = strdup(hwalk->clientmsg);
					msg->buflen = strlen(msg->buf);
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

	dprintf("<- do_message/%d\n", nesting);
	nesting--;
}


void save_checkpoint(void)
{
	char *tempfn;
	FILE *fd;
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk;
	hobbitd_log_t *lwalk;
	time_t now = time(NULL);
	scheduletask_t *swalk;
	ackinfo_t *awalk;
	int iores = 0;

	if (checkpointfn == NULL) return;

	dprintf("-> save_checkpoint\n");
	tempfn = malloc(strlen(checkpointfn) + 20);
	sprintf(tempfn, "%s.%d", checkpointfn, (int)now);
	fd = fopen(tempfn, "w");
	if (fd == NULL) {
		errprintf("Cannot open checkpoint file %s : %s\n", tempfn, strerror(errno));
		xfree(tempfn);
		return;
	}

	for (hosthandle = rbtBegin(rbhosts); ((hosthandle != rbtEnd(rbhosts)) && (iores >= 0)); hosthandle = rbtNext(rbhosts, hosthandle)) {
		char *msgstr;

		hwalk = gettreeitem(rbhosts, hosthandle);

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
			flush_acklist(lwalk, 0);
			iores = fprintf(fd, "@@HOBBITDCHK-V1|%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%s", 
				lwalk->origin->name, hwalk->hostname, lwalk->test->testname, lwalk->sender,
				colnames[lwalk->color], 
				(lwalk->testflags ? lwalk->testflags : ""),
				colnames[lwalk->oldcolor],
				(int)lwalk->logtime, (int) lwalk->lastchange, (int) lwalk->validtime, 
				(int) lwalk->enabletime, (int) lwalk->acktime, 
				lwalk->cookie, (int) lwalk->cookieexpires,
				nlencode(lwalk->message));
			if (lwalk->dismsg) msgstr = nlencode(lwalk->dismsg); else msgstr = "";
			if (iores >= 0) iores = fprintf(fd, "|%s", msgstr);
			if (lwalk->ackmsg) msgstr = nlencode(lwalk->ackmsg); else msgstr = "";
			if (iores >= 0) iores = fprintf(fd, "|%s", msgstr);
			if (iores >= 0) iores = fprintf(fd, "\n");

			for (awalk = lwalk->acklist; (awalk && (iores >= 0)); awalk = awalk->next) {
				iores = fprintf(fd, "@@HOBBITDCHK-V1|.acklist.|%s|%s|%d|%d|%d|%s|%s\n",
						hwalk->hostname, lwalk->test->testname,
			 			(int)awalk->received, (int)awalk->validuntil, awalk->level, 
						awalk->ackedby, awalk->msg);
			}
		}
	}

	for (swalk = schedulehead; (swalk && (iores >= 0)); swalk = swalk->next) {
		iores = fprintf(fd, "@@HOBBITDCHK-V1|.task.|%d|%d|%s|%s\n", 
			swalk->id, (int)swalk->executiontime, swalk->sender, nlencode(swalk->command));
	}

	if (iores < 0) {
		errprintf("I/O error while saving the checkpoint file: %s\n", strerror(errno));
		exit(1);
	}

	iores = fclose(fd);
	if (iores == EOF) {
		errprintf("I/O error while closing the checkpoint file: %s\n", strerror(errno));
		exit(1);
	}

	iores = rename(tempfn, checkpointfn);
	if (iores == -1) {
		errprintf("I/O error while renaming the checkpoint file: %s\n", strerror(errno));
		exit(1);
	}

	xfree(tempfn);
	dprintf("<- save_checkpoint\n");
}


void load_checkpoint(char *fn)
{
	FILE *fd;
	char *inbuf = NULL;
	int inbufsz;
	char *item;
	int i, err;
	char hostip[20];
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hitem = NULL;
	hobbitd_testlist_t *t = NULL;
	hobbitd_log_t *ltail = NULL;
	htnames_t *origin = NULL;
	char *originname = NULL, *hostname = NULL, *testname = NULL, *sender = NULL, *testflags = NULL; 
	char *statusmsg = NULL, *disablemsg = NULL, *ackmsg = NULL;
	time_t logtime = 0, lastchange = 0, validtime = 0, enabletime = 0, acktime = 0, cookieexpires = 0;
	int color = COL_GREEN, oldcolor = COL_GREEN, cookie = -1;
	int count = 0;

	fd = fopen(fn, "r");
	if (fd == NULL) {
		errprintf("Cannot access checkpoint file %s for restore\n", fn);
		return;
	}

	MEMDEFINE(hostip);

	initfgets(fd);
	while (unlimfgets(&inbuf, &inbufsz, fd)) {
		hostname = testname = sender = testflags = statusmsg = disablemsg = ackmsg = NULL;
		lastchange = validtime = enabletime = acktime = 0;
		err = 0;

		if (strncmp(inbuf, "@@HOBBITDCHK-V1|.task.|", 23) == 0) {
			scheduletask_t *newtask = (scheduletask_t *)calloc(1, sizeof(scheduletask_t));

			item = gettok(inbuf, "|\n"); i = 0;
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

		if (strncmp(inbuf, "@@HOBBITDCHK-V1|.acklist.|", 26) == 0) {
			hobbitd_log_t *log = NULL;
			ackinfo_t *newack = (ackinfo_t *)calloc(1, sizeof(ackinfo_t));

			hitem = NULL;

			item = gettok(inbuf, "|\n"); i = 0;
			while (item) {

				switch (i) {
				  case 0: break;
				  case 1: break;
				  case 2: 
					hosthandle = rbtFind(rbhosts, item); 
					hitem = gettreeitem(rbhosts, hosthandle);
					break;
				  case 3: 
					for (t=tests; (t && (strcmp(t->testname, item) != 0)); t = t->next) ;
					break;
				  case 4: newack->received = atoi(item); break;
				  case 5: newack->validuntil = atoi(item); break;
				  case 6: newack->level = atoi(item); break;
				  case 7: newack->ackedby = strdup(item); break;
				  case 8: newack->msg = strdup(item); break;
				  default: break;
				}
				item = gettok(NULL, "|\n"); i++;
			}

			if (hitem && t) {
				for (log = hitem->logs; (log && (log->test != t)); log = log->next) ;
			}

			if (log) {
				newack->next = log->acklist;
				log->acklist = newack;
			}
			else {
				if (newack->ackedby) xfree(newack->ackedby);
				if (newack->msg) xfree(newack->msg);
				xfree(newack);
			}

			continue;
		}

		if (strncmp(inbuf, "@@HOBBITDCHK-V1|.", 17) == 0) continue;

		item = gettok(inbuf, "|\n"); i = 0;
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
		hostname = knownhost(hostname, hostip, ghosthandling);
		if (hostname == NULL) continue;

		/* Ignore the "info" and "trends" data, since we generate on the fly now. */
		if (strcmp(testname, xgetenv("INFOCOLUMN")) == 0) continue;
		if (strcmp(testname, xgetenv("TRENDSCOLUMN")) == 0) continue;

		dprintf("Status: Host=%s, test=%s\n", hostname, testname); count++;

		hosthandle = rbtFind(rbhosts, hostname);
		if (hosthandle == rbtEnd(rbhosts)) {
			/* New host */
			hitem = (hobbitd_hostlist_t *) malloc(sizeof(hobbitd_hostlist_t));
			hitem->hostname = strdup(hostname);
			strcpy(hitem->ip, hostip);
			hitem->logs = NULL;
			hitem->clientmsg = NULL;
			rbtInsert(rbhosts, hitem->hostname, hitem);
			hostcount++;
		}
		else {
			hitem = gettreeitem(rbhosts, hosthandle);
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

		if (hitem->logs == NULL) {
			ltail = hitem->logs = (hobbitd_log_t *) malloc(sizeof(hobbitd_log_t));
		}
		else {
			ltail->next = (hobbitd_log_t *)malloc(sizeof(hobbitd_log_t));
			ltail = ltail->next;
		}

		/* Fixup validtime in case of ack'ed or disabled tests */
		if (validtime < acktime) validtime = acktime;
		if (validtime < enabletime) validtime = enabletime;

		ltail->test = t;
		ltail->host = hitem;
		ltail->origin = origin;
		ltail->color = color;
		ltail->oldcolor = oldcolor;
		ltail->activealert = (decide_alertstate(color) == A_ALERT);
		ltail->histsynced = 0;
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
		ltail->metas = NULL;
		ltail->acklist = NULL;
		ltail->next = NULL;
	}

	fclose(fd);
	if (inbuf) xfree(inbuf);
	dprintf("Loaded %d status logs\n", count);

	MEMDEFINE(hostip);
}


void check_purple_status(void)
{
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk;
	hobbitd_log_t *lwalk;
	time_t now = time(NULL);

	dprintf("-> check_purple_status\n");
	for (hosthandle = rbtBegin(rbhosts); (hosthandle != rbtEnd(rbhosts)); hosthandle = rbtNext(rbhosts, hosthandle)) {
		hwalk = gettreeitem(rbhosts, hosthandle);

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

					/* Tests on dialup hosts go clear, not purple */
					if (newcolor == COL_PURPLE) {
						namelist_t *hinfo = hostinfo(hwalk->hostname);
						if (hinfo && bbh_item(hinfo, BBH_FLAG_DIALUP)) newcolor = COL_CLEAR;
					}

					handle_status(lwalk->message, "hobbitd", 
						hwalk->hostname, lwalk->test->testname, lwalk, newcolor, NULL);
					lwalk = lwalk->next;
				}
			}
			else {
				lwalk = lwalk->next;
			}
		}
	}
	dprintf("<- check_purple_status\n");
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
	time_t conn_timeout = 30;
	time_t nextheartbeat = 0;
	char *envarea = NULL;

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
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--trace=")) {
			char *p = strchr(argv[argi], '=');
			tracelist = getsenderlist(p+1);
		}
		else if (strcmp(argv[argi], "--trace-all") == 0) {
			traceall = 1;
		}
		else if (strcmp(argv[argi], "--ignore-traced") == 0) {
			ignoretraced = 1;
		}
		else if (strcmp(argv[argi], "--no-clientlog") == 0) {
			 save_clientlogs = 0;
		}
		else if (argnmatch(argv[argi], "--help")) {
			printf("Options:\n");
			printf("\t--listen=IP:PORT              : The address the daemon listens on\n");
			printf("\t--bbhosts=FILENAME            : The bb-hosts file\n");
			printf("\t--ghosts=allow|drop|log       : How to handle unknown hosts\n");
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

	rbhosts = rbtNew(hostname_compare);

	errprintf("Loading hostnames\n");
	load_hostnames(bbhostsfn, NULL, get_fqdn());

	if (restartfn) {
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
	clientchn  = setup_channel(C_CLIENT, CHAN_MASTER);
	if (clientchn == NULL) { errprintf("Cannot setup client channel\n"); return 1; }

	errprintf("Setting up logfiles\n");
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	freopen("/dev/null", "r", stdin);
	if (logfn) {
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	if (dbghost) {
		char fname[PATH_MAX];

		sprintf(fname, "%s/hobbitd.dbg", xgetenv("BBTMP"));
		dbgfd = fopen(fname, "a");
		if (dbgfd == NULL) errprintf("Cannot open debug file %s: %s\n", fname, strerror(errno));
	}

	if (!daemonize) {
		/* Setup to send the parent proces a heartbeat-signal (SIGUSR2) */
		nextheartbeat = time(NULL);
		parentpid = getppid(); if (parentpid <= 1) parentpid = 0;
	}

	errprintf("Setup complete\n");
	do {
		/*
		 * The endless loop.
		 *
		 * First attend to the housekeeping chores:
		 * - send out our heartbeat signal;
		 * - pick up children to avoid zombies;
		 * - rotate logs, if we have been asked to;
		 * - re-load the bb-hosts configuration if needed;
		 * - check for stale status-logs that must go purple;
		 * - inject our own statistics message.
		 * - save the checkpoint file;
		 *
		 * Then do the network I/O.
		 */
		struct timeval seltmo;
		fd_set fdread, fdwrite;
		int maxfd, n;
		conn_t *cwalk;
		time_t now = time(NULL);
		int childstat;

		if (parentpid && (nextheartbeat <= now)) {
			dprintf("Sending heartbeat to pid %d\n", (int) parentpid);
			nextheartbeat = now + 5;
			kill(parentpid, SIGUSR2);
		}

		/* Pickup any finished child processes to avoid zombies */
		while (wait3(&childstat, WNOHANG, NULL) > 0) ;

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
			posttochannel(clientchn, "logrotate", NULL, "hobbitd", NULL, NULL, "");
		}

		if (reloadconfig && bbhostsfn) {
			RbtIterator hosthandle;

			reloadconfig = 0;
			load_hostnames(bbhostsfn, NULL, get_fqdn());

			/* Scan our list of hosts and weed out those we do not know about any more */
			hosthandle = rbtBegin(rbhosts);
			while (hosthandle != rbtEnd(rbhosts)) {
				hobbitd_hostlist_t *hwalk;

				hwalk = gettreeitem(rbhosts, hosthandle);

				if (strcmp(hwalk->hostname, "summary") == 0) {
					/* Leave the summaries as-is */
					hosthandle = rbtNext(rbhosts, hosthandle);
				}
				else if (hostinfo(hwalk->hostname) == NULL) {
					/* Remove all state info about this host. This will NOT remove files. */
					handle_dropnrename(CMD_DROPSTATE, "hobbitd", hwalk->hostname, NULL, NULL);

					/* Must restart tree-walk after deleting node from the tree */
					hosthandle = rbtBegin(rbhosts);
				}
				else {
					hosthandle = rbtNext(rbhosts, hosthandle);
				}
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
			get_hts(buf, "hobbitd", "", &h, &t, &log, &color, NULL, NULL, 1, 1);
			if (!h || !t || !log) {
				errprintf("hobbitd servername MACHINE='%s' not listed in bb-hosts, dropping hobbitd status\n",
					  xgetenv("MACHINE"));
			}
			else {
				handle_status(buf, "hobbitd", h->hostname, t->testname, log, color, NULL);
			}
			last_stats_time = now;
			flush_errbuf();
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

		/*
		 * Prepare for the network I/O.
		 * Find the largest open socket we have, from our active sockets,
		 * and setup the select() FD sets.
		 */
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

		/* 
		 * Do the select() with a static 2 second timeout. 
		 * This is long enough that we will suspend activity for
		 * some time if there's nothing to do, but short enough for
		 * us to attend to the housekeeping stuff without undue delay.
		 */
		seltmo.tv_sec = 2; seltmo.tv_usec = 0;
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

		/*
		 * Now do the actual data exchange over the net.
		 */
		for (cwalk = connhead; (cwalk); cwalk = cwalk->next) {
			switch (cwalk->doingwhat) {
			  case RECEIVING:
				if (FD_ISSET(cwalk->sock, &fdread)) {
					n = read(cwalk->sock, cwalk->bufp, (cwalk->bufsz - cwalk->buflen - 1));
					if (n <= 0) {
						/* End of input data on this connection */
						*(cwalk->bufp) = '\0';

						/* FIXME - need to set origin here */
						do_message(cwalk, "");
					}
					else {
						/* Add data to the input buffer - within reason ... */
						cwalk->bufp += n;
						cwalk->buflen += n;
						*(cwalk->bufp) = '\0';
						if ((cwalk->bufsz - cwalk->buflen) < 2048) {
							if (cwalk->bufsz < MAX_HOBBIT_INBUFSZ) {
								cwalk->bufsz += HOBBIT_INBUF_INCREMENT;
								cwalk->buf = (unsigned char *) realloc(cwalk->buf, cwalk->bufsz);
								cwalk->bufp = cwalk->buf + cwalk->buflen;
							}
							else {
								/* Someone is flooding us */
								errprintf("Data flooding from %s, closing connection\n",
									  inet_ntoa(cwalk->addr.sin_addr));
								shutdown(cwalk->sock, SHUT_RDWR);
								close(cwalk->sock); 
								cwalk->sock = -1; 
								cwalk->doingwhat = NOTALK;
							}
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
				conntail->bufsz = HOBBIT_INBUF_INITIAL;
				conntail->buf = (unsigned char *)malloc(conntail->bufsz);
				conntail->bufp = conntail->buf;
				conntail->buflen = 0;
				conntail->timeout = now + conn_timeout;
				conntail->next = NULL;
			}
		}
	} while (running);

	/* Tell the workers we to shutdown also */
	running = 1;   /* Kludge, but it's the only way to get posttochannel to do something. */
	posttochannel(statuschn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(stachgchn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(pagechn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(datachn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(noteschn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(enadischn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(clientchn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	running = 0;

	/* Close the channels */
	close_channel(statuschn, CHAN_MASTER);
	close_channel(stachgchn, CHAN_MASTER);
	close_channel(pagechn, CHAN_MASTER);
	close_channel(datachn, CHAN_MASTER);
	close_channel(noteschn, CHAN_MASTER);
	close_channel(enadischn, CHAN_MASTER);
	close_channel(clientchn, CHAN_MASTER);

	save_checkpoint();
	unlink(pidfile);

	if (dbgfd) fclose(dbgfd);

	MEMUNDEFINE(colnames);

	return 0;
}

