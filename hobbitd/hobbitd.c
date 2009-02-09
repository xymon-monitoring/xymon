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
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd.c,v 1.253 2006-08-03 18:59:02 henrik Exp $";

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

#define DISABLED_UNTIL_OK -1

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

typedef struct testinfo_t {
	char *name;
	int clientsave;
} testinfo_t;

/* This holds all information about a single status */
typedef struct hobbitd_log_t {
	struct hobbitd_hostlist_t *host;
	testinfo_t *test;
	char *origin;
	int color, oldcolor, activealert, histsynced, downtimeactive;
	char *testflags;
	char *grouplist;        /* For extended status reports (e.g. from hobbitd_client) */
	char sender[IP_ADDR_STRLEN];
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
	char ip[IP_ADDR_STRLEN];
	enum { H_NORMAL, H_SUMMARY } hosttype;
	hobbitd_log_t *logs;
	hobbitd_log_t *pinglog; /* Points to entry in logs list, but we need it often */
	time_t clientmsgtstamp;
	char *clientmsg;
	int clientmsgposted;
} hobbitd_hostlist_t;

typedef struct filecache_t {
	char *fn;
	long len;
	unsigned char *fdata;
} filecache_t;

RbtHandle rbhosts;				/* The hosts we have reports from */
RbtHandle rbtests;				/* The tests (columns) we have seen */
RbtHandle rborigins;				/* The origins we have seen */
RbtHandle rbcookies;				/* The cookies we use */
RbtHandle rbfilecache;

sender_t *maintsenders = NULL;
sender_t *statussenders = NULL;
sender_t *adminsenders = NULL;
sender_t *wwwsenders = NULL;
sender_t *tracelist = NULL;
int      traceall = 0;
int      ignoretraced = 0;
int      clientsavemem = 1;	/* In memory */
int      clientsavedisk = 0;	/* On disk via the CLICHG channel */
int      allow_downloads = 1;

#define NOTALK 0
#define RECEIVING 1
#define RESPONDING 2

/* This struct describes an active connection with a Hobbit client */
typedef struct conn_t {
	int sock;			/* Communications socket */
	struct sockaddr_in addr;	/* Client source address */
	unsigned char *buf, *bufp;	/* Message buffer and pointer */
	size_t buflen, bufsz;		/* Active and maximum length of buffer */
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
hobbitd_channel_t *clichgchn = NULL;	/* Receives "clichg" messages */
hobbitd_channel_t *userchn   = NULL;	/* Receives "usermsg" messages */

#define NO_COLOR (COL_COUNT)
static char *colnames[COL_COUNT+1];
int alertcolors, okcolors;
enum alertstate_t { A_OK, A_ALERT, A_UNDECIDED };

typedef struct ghostlist_t {
	char *name;
	char *sender;
	time_t tstamp;
} ghostlist_t;
RbtHandle rbghosts;

int ghosthandling = -1;

char *checkpointfn = NULL;
FILE *dbgfd = NULL;
char *dbghost = NULL;
time_t boottime;
int  hostcount = 0;
char *ackinfologfn = NULL;
FILE *ackinfologfd = NULL;

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
	{ "ping", 0 },
	{ "notify", 0 },
	{ "schedule", 0 },
	{ "download", 0 },
	{ NULL, 0 }
};

enum boardfield_t { F_NONE, F_HOSTNAME, F_TESTNAME, F_COLOR, F_FLAGS, 
		    F_LASTCHANGE, F_LOGTIME, F_VALIDTIME, F_ACKTIME, F_DISABLETIME,
		    F_SENDER, F_COOKIE, F_LINE1,
		    F_ACKMSG, F_DISMSG, F_MSG, F_CLIENT, F_CLIENTTSTAMP,
		    F_ACKLIST,
		    F_HOSTINFO,
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
	{ "clntstamp", F_CLIENTTSTAMP },
	{ "acklist", F_ACKLIST },
	{ "BBH_", F_HOSTINFO },
	{ NULL, F_LAST },
};
typedef struct boardfields_t {
	enum boardfield_t field;
	enum bbh_item_t bbhfield;
} boardfield_t;
#define BOARDFIELDS_MAX 50
boardfield_t boardfields[BOARDFIELDS_MAX+1];

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

	dbgprintf("-> update_statistics\n");

	if (!cmd) {
		dbgprintf("No command for update_statistics\n");
		return;
	}

	msgs_total++;

	i = 0;
	while (hobbitd_stats[i].cmd && strncmp(hobbitd_stats[i].cmd, cmd, strlen(hobbitd_stats[i].cmd))) { i++; }
	hobbitd_stats[i].count++;

	dbgprintf("<- update_statistics\n");
}

char *generate_stats(void)
{
	static char *statsbuf = NULL;
	static int statsbuflen = 0;
	time_t now = getcurrenttime(NULL);
	char *bufp;
	int i, clients;
	char bootuptxt[40];
	char uptimetxt[40];
	RbtHandle ghandle;
	time_t uptime = (now - boottime);

	dbgprintf("-> generate_stats\n");

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
	clients = semctl(clichgchn->semid, CLIENTCOUNT, GETVAL);
	bufp += sprintf(bufp, "clichg channel messages: %10ld (%d readers)\n", clichgchn->msgcount, clients);

	ghandle = rbtBegin(rbghosts);
	if (ghandle != rbtEnd(rbghosts)) bufp += sprintf(bufp, "\n\nGhost reports:\n");
	for (; (ghandle != rbtEnd(rbghosts)); ghandle = rbtNext(rbghosts, ghandle)) {
		ghostlist_t *gwalk = (ghostlist_t *)gettreeitem(rbghosts, ghandle);

		/* Skip records older than 10 minutes */
		if (gwalk->tstamp < (now - 600)) continue;

		if ((statsbuflen - (bufp - statsbuf)) < 512) {
			/* Less than 512 bytes left in buffer - expand it */
			statsbuflen += 4096;
			statsbuf = (char *)realloc(statsbuf, statsbuflen);
			bufp = statsbuf + strlen(statsbuf);
		}

		bufp += sprintf(bufp, "  %-15s reported host %s\n", gwalk->sender, gwalk->name);
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

	dbgprintf("<- generate_stats\n");

	return statsbuf;
}


enum alertstate_t decide_alertstate(int color)
{
	if ((okcolors & (1 << color)) != 0) return A_OK;
	else if ((alertcolors & (1 << color)) != 0) return A_ALERT;
	else return A_UNDECIDED;
}


hobbitd_hostlist_t *create_hostlist_t(char *hostname, char *ip)
{
	hobbitd_hostlist_t *hitem;

	hitem = (hobbitd_hostlist_t *) calloc(1, sizeof(hobbitd_hostlist_t));
	hitem->hostname = strdup(hostname);
	strcpy(hitem->ip, ip);
	if (strcmp(hostname, "summary") == 0) hitem->hosttype = H_SUMMARY;
	else hitem->hosttype = H_NORMAL;
	rbtInsert(rbhosts, hitem->hostname, hitem);

	return hitem;
}

testinfo_t *create_testinfo(char *name)
{
	testinfo_t *newrec;

	newrec = (testinfo_t *)calloc(1, sizeof(testinfo_t));
	newrec->name = strdup(name);
	newrec->clientsave = clientsavedisk;
	rbtInsert(rbtests, newrec->name, newrec);

	return newrec;
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
	unsigned int bufsz = 1024*shbufsz(channel->channelid);
	void *hi;
	char *pagepath, *classname, *osname;

	dbgprintf("-> posttochannel\n");

	/* First see how many users are on this channel */
	clients = semctl(channel->semid, CLIENTCOUNT, GETVAL);
	if (clients == 0) {
		dbgprintf("Dropping message - no readers\n");
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

	/* Check if we failed to grab the semaphore */
	if (n == -1) {
		errprintf("Dropping message due to semaphore error\n");
		return;
	}

	/* All clear, post the message */
	if (channel->seq == 999999) channel->seq = 0;
	channel->seq++;
	channel->msgcount++;
	gettimeofday(&tstamp, &tz);
	if (readymsg) {
		n = snprintf(channel->channelbuf, (bufsz-5),
			    "@@%s#%u/%s|%d.%06d|%s|%s", 
			    channelmarker, channel->seq, 
			    (hostname ? hostname : "*"), 
			    (int) tstamp.tv_sec, (int) tstamp.tv_usec,
			    sender, readymsg);
		if (n > (bufsz-5)) {
			char *p, *overmsg = readymsg;
			*(overmsg+100) = '\0';
			p = strchr(overmsg, '\n'); if (p) *p = '\0';
			errprintf("Oversize data/client msg from %s truncated (n=%d, limit %d)\nFirst line: %s\n", 
				   sender, n, bufsz, overmsg);
		}
		*(channel->channelbuf + bufsz - 5) = '\0';
	}
	else {
		switch(channel->channelid) {
		  case C_STATUS:
			hi = hostinfo(hostname);
			pagepath = (hi ? bbh_item(hi, BBH_ALLPAGEPATHS) : "");
			classname = (hi ? bbh_item(hi, BBH_CLASS) : "");
			if (!classname) classname = "";

			n = snprintf(channel->channelbuf, (bufsz-5),
				"@@%s#%u/%s|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%s|%d", 
				channelmarker, channel->seq, hostname, 		/*  0 */
				(int) tstamp.tv_sec, (int) tstamp.tv_usec,	/*  1 */
				sender, 					/*  2 */
				log->origin, 					/*  3 */
				hostname, 					/*  4 */
				log->test->name, 				/*  5 */
				(int) log->validtime, 				/*  6 */
				colnames[log->color], 				/*  7 */
				(log->testflags ? log->testflags : ""),		/*  8 */
				colnames[log->oldcolor], 			/*  9 */
				(int) log->lastchange); 			/* 10 */
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d|%s",	/* 11+12 */
					(int)log->acktime, nlencode(log->ackmsg));
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d|%s",	/* 13+14 */
					(int)log->enabletime, nlencode(log->dismsg));
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d",	/* 15 */
					(int)log->host->clientmsgtstamp);
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%s", classname);	/* 16 */
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%s", pagepath);	/* 17 */
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "\n%s", msg);
			}
			if (n > (bufsz-5)) {
				errprintf("Oversize status msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, log->test->name, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_STACHG:
			n = snprintf(channel->channelbuf, (bufsz-5),
				"@@%s#%u/%s|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%d", 
				channelmarker, channel->seq, hostname, 		/*  0 */
				(int) tstamp.tv_sec, (int) tstamp.tv_usec,	/*  1 */
				sender,						/*  2 */ 
				log->origin,					/*  3 */ 
				hostname,					/*  4 */ 
				log->test->name,				/*  5 */ 
				(int) log->validtime,				/*  6 */ 
				colnames[log->color],				/*  7 */ 
				colnames[log->oldcolor],			/*  8 */ 
				(int) log->lastchange)			/*  9 */;
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d|%s",	/* 10+11 */
					(int)log->enabletime, nlencode(log->dismsg));
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d", 	/* 12 */
						log->downtimeactive);
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "|%d", 	/* 13 */
						(int) log->host->clientmsgtstamp);
			}
			if (n < (bufsz-5)) {
				n += snprintf(channel->channelbuf+n, (bufsz-n-5), "\n%s", msg);
			}
			if (n > (bufsz-5)) {
				errprintf("Oversize stachg msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, log->test->name, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_CLICHG:
			n = snprintf(channel->channelbuf, (bufsz-5),
				"@@%s#%u/%s|%d.%06d|%s|%s|%d\n%s",
				channelmarker, channel->seq, hostname, (int) tstamp.tv_sec, (int) tstamp.tv_usec,
				sender, hostname, (int) log->host->clientmsgtstamp, 
				log->host->clientmsg);
			if (n > (bufsz-5)) {
				errprintf("Oversize clichg msg from %s for %s truncated (n=%d, limit=%d)\n", 
					sender, hostname, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_PAGE:
			if (strcmp(channelmarker, "ack") == 0) {
				n = snprintf(channel->channelbuf, (bufsz-5),
					"@@%s#%u/%s|%d.%06d|%s|%s|%s|%s|%d\n%s", 
					channelmarker, channel->seq, hostname, (int) tstamp.tv_sec, (int) tstamp.tv_usec,
					sender, hostname, 
					log->test->name, log->host->ip,
					(int) log->acktime, msg);
			}
			else {
				hi = hostinfo(hostname);
				pagepath = (hi ? bbh_item(hi, BBH_ALLPAGEPATHS) : "");
				classname = (hi ? bbh_item(hi, BBH_CLASS) : "");
				osname = (hi ? bbh_item(hi, BBH_OS) : "");
				if (!classname) classname = "";
				if (!osname) osname = "";

				n = snprintf(channel->channelbuf, (bufsz-5),
					"@@%s#%u/%s|%d.%06d|%s|%s|%s|%s|%d|%s|%s|%d|%s|%d|%s|%s|%s\n%s", 
					channelmarker, channel->seq, hostname, (int) tstamp.tv_sec, (int) tstamp.tv_usec,
					sender, hostname, 
					log->test->name, log->host->ip, (int) log->validtime, 
					colnames[log->color], colnames[log->oldcolor], (int) log->lastchange,
					pagepath, log->cookie, osname, classname, 
					(log->grouplist ? log->grouplist : ""),
					msg);
			}
			if (n > (bufsz-5)) {
				errprintf("Oversize page/ack/notify msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, (log->test->name ? log->test->name : "<none>"), n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_DATA:
		  case C_CLIENT:
			/* Data channel messages are pre-formatted so we never go here */
			break;

		  case C_NOTES:
		  case C_USER:
			n = snprintf(channel->channelbuf,  (bufsz-5),
				"@@%s#%u/%s|%d.%06d|%s|%s\n%s", 
				channelmarker, channel->seq, hostname, (int) tstamp.tv_sec, (int) tstamp.tv_usec,
				sender, hostname, msg);
			if (n > (bufsz-5)) {
				errprintf("Oversize notes/user msg from %s for %s truncated (n=%d, limit=%d)\n", 
					sender, hostname, n, bufsz);
			}
			*(channel->channelbuf + bufsz - 5) = '\0';
			break;

		  case C_ENADIS:
			n = snprintf(channel->channelbuf, (bufsz-5),
				"@@%s#%u/%s|%d.%06d|%s|%s|%s|%d",
				channelmarker, channel->seq, hostname, (int) tstamp.tv_sec, (int)tstamp.tv_usec,
				sender, hostname, log->test->name, (int) log->enabletime);
			if (n > (bufsz-5)) {
				errprintf("Oversize enadis msg from %s for %s:%s truncated (n=%d, limit=%d)\n", 
					sender, hostname, log->test->name, n, bufsz);
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
	dbgprintf("Posting message %u to %d readers\n", channel->seq, clients);
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

	dbgprintf("<- posttochannel\n");

	return;
}


void log_ghost(char *hostname, char *sender, char *msg)
{
	RbtHandle ghandle;
	ghostlist_t *gwalk;

	dbgprintf("-> log_ghost\n");

	/* If debugging, log the full request */
	if (dbgfd) {
		fprintf(dbgfd, "\n---- combo message from %s ----\n%s---- end message ----\n", sender, msg);
		fflush(dbgfd);
	}

	if ((hostname == NULL) || (sender == NULL)) return;

	ghandle = rbtFind(rbghosts, hostname);
	if (ghandle == rbtEnd(rbghosts)) {
		gwalk = (ghostlist_t *)malloc(sizeof(ghostlist_t));
		gwalk->name = strdup(hostname);
		gwalk->sender = strdup(sender);
		gwalk->tstamp = getcurrenttime(NULL);
		rbtInsert(rbghosts, gwalk->name, gwalk);
	}
	else {
		gwalk = (ghostlist_t *)gettreeitem(rbghosts, ghandle);
		if (gwalk->sender) xfree(gwalk->sender);
		gwalk->sender = strdup(sender);
		gwalk->tstamp = getcurrenttime(NULL);
	}

	dbgprintf("<- log_ghost\n");
}

hobbitd_log_t *find_log(char *hostname, char *testname, char *origin, hobbitd_hostlist_t **host)
{
	RbtIterator hosthandle, testhandle, originhandle;
	hobbitd_hostlist_t *hwalk;
	char *owalk = NULL;
	testinfo_t *twalk;
	hobbitd_log_t *lwalk;

	*host = NULL;
	if ((hostname == NULL) || (testname == NULL)) return NULL;

	hosthandle = rbtFind(rbhosts, hostname);
	if (hosthandle != rbtEnd(rbhosts)) *host = hwalk = gettreeitem(rbhosts, hosthandle); else return NULL;

	testhandle = rbtFind(rbtests, testname);
	if (testhandle != rbtEnd(rbtests)) twalk = gettreeitem(rbtests, testhandle); else return NULL;

	if (origin) {
		originhandle = rbtFind(rborigins, origin);
		if (originhandle != rbtEnd(rborigins)) owalk = gettreeitem(rborigins, originhandle);
	}

	for (lwalk = hwalk->logs; (lwalk && ((lwalk->test != twalk) || (lwalk->origin != owalk))); lwalk = lwalk->next);
	return lwalk;
}

void get_hts(char *msg, char *sender, char *origin,
	     hobbitd_hostlist_t **host, testinfo_t **test, char **grouplist, hobbitd_log_t **log, 
	     int *color, char **downcause, int *alltests, int createhost, int createlog)
{
	/*
	 * This routine takes care of finding existing status log records, or
	 * (if they dont exist) creating new ones for an incoming status.
	 *
	 * "msg" contains an incoming message. First list is of the form "KEYWORD host,domain.test COLOR"
	 */

	char *firstline, *p;
	char *hosttest, *hostname, *testname, *colstr, *grp;
	char hostip[IP_ADDR_STRLEN];
	RbtIterator hosthandle, testhandle, originhandle;
	hobbitd_hostlist_t *hwalk = NULL;
	testinfo_t *twalk = NULL;
	char *owalk = NULL;
	hobbitd_log_t *lwalk = NULL;

	dbgprintf("-> get_hts\n");

	MEMDEFINE(hostip);
	*hostip = '\0';

	*host = NULL;
	*test = NULL;
	*log = NULL;
	*color = -1;
	if (grouplist) *grouplist = NULL;
	if (downcause) *downcause = NULL;
	if (alltests) *alltests = 0;

	hosttest = hostname = testname = colstr = grp = NULL;
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
	if (p) {
		/* There might be a group-list */
		grp = strstr(p, "/group:");
		if (grp) grp += 7;
	}
	if (p) hosttest = strtok(NULL, " \t"); /* ... HOST.TEST combo ... */
	if (hosttest == NULL) goto done;
	colstr = strtok(NULL, " \t"); /* ... and the color (if any) */
	if (colstr) {
		*color = parse_color(colstr);
		/* Dont create log-entries if we get a bad color spec. */
		if (*color == -1) createlog = 0;
	}
	else createlog = 0;

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
		hwalk = create_hostlist_t(hostname, hostip);
		hostcount++;
	}

	if (testname && *testname) {
		if (alltests && (*testname == '*')) {
			*alltests = 1;
			return;
		}

		testhandle = rbtFind(rbtests, testname);
		if (testhandle != rbtEnd(rbtests)) twalk = gettreeitem(rbtests, testhandle);
		if (createlog && (twalk == NULL)) twalk = create_testinfo(testname);
	}
	else {
		if (createlog) errprintf("Bogus message from %s: No testname '%s'\n", sender, msg);
	}

	if (origin) {
		originhandle = rbtFind(rborigins, origin);
		if (originhandle != rbtEnd(rborigins)) owalk = gettreeitem(rborigins, originhandle);
		if (createlog && (owalk == NULL)) {
			owalk = strdup(origin);
			rbtInsert(rborigins, owalk, owalk);
		}
	}

	if (hwalk && twalk && owalk) {
		for (lwalk = hwalk->logs; (lwalk && ((lwalk->test != twalk) || (lwalk->origin != owalk))); lwalk = lwalk->next);
		if (createlog && (lwalk == NULL)) {
			lwalk = (hobbitd_log_t *)calloc(1, sizeof(hobbitd_log_t));
			lwalk->color = lwalk->oldcolor = NO_COLOR;
			lwalk->host = hwalk;
			lwalk->test = twalk;
			lwalk->origin = owalk;
			lwalk->cookie = -1;
			lwalk->next = hwalk->logs;
			hwalk->logs = lwalk;
			if (strcmp(testname, xgetenv("PINGCOLUMN")) == 0) hwalk->pinglog = lwalk;
		}
	}

done:
	if (colstr) {
		if ((*color == COL_RED) || (*color == COL_YELLOW)) {
			char *cause;

			cause = check_downtime(hostname, testname);
			if (lwalk) lwalk->downtimeactive = (cause != NULL);
			if (cause) *color = COL_BLUE;
			if (downcause) *downcause = cause;
		}
		else {
			if (lwalk) lwalk->downtimeactive = 0;
		}
	}

	if (grouplist && grp) *grouplist = strdup(grp);

	xfree(firstline);

	*host = hwalk;
	*test = twalk;
	*log = lwalk;

	MEMUNDEFINE(hostip);

	dbgprintf("<- get_hts\n");
}


hobbitd_log_t *find_cookie(int cookie)
{
	/*
	 * Find a cookie we have issued.
	 */
	hobbitd_log_t *result = NULL;
	RbtIterator cookiehandle;

	dbgprintf("-> find_cookie\n");

	cookiehandle = rbtFind(rbcookies, (void *)cookie);
	if (cookiehandle != rbtEnd(rbcookies)) {
		result = gettreeitem(rbcookies, cookiehandle);
		if (result->cookieexpires <= getcurrenttime(NULL)) result = NULL;
	}

	dbgprintf("<- find_cookie\n");

	return result;
}

void clear_cookie(hobbitd_log_t *log)
{
	RbtIterator cookiehandle;

	if (log->cookie <= 0) return;

	cookiehandle = rbtFind(rbcookies, (void *)log->cookie);
	log->cookie = -1; log->cookieexpires = 0;

	if (cookiehandle == rbtEnd(rbcookies)) return;

	rbtErase(rbcookies, cookiehandle);
}


void handle_status(unsigned char *msg, char *sender, char *hostname, char *testname, char *grouplist, 
		   hobbitd_log_t *log, int newcolor, char *downcause)
{
	int validity = 30;	/* validity is counted in minutes */
	time_t now = getcurrenttime(NULL);
	int msglen, issummary;
	enum alertstate_t oldalertstatus, newalertstatus;

	dbgprintf("->handle_status\n");

	if (msg == NULL) {
		errprintf("handle_status got a NULL message for %s.%s, sender %s, color %s\n", 
			  textornull(hostname), textornull(testname), textornull(sender), colorname(newcolor));
		return;
	}

	msglen = strlen(msg);
	if (msglen == 0) {
		errprintf("Bogus status message for %s.%s contains no data: Sent from %s\n", 
			  textornull(hostname), textornull(testname), textornull(sender));
		return;
	}
	if (msg_data(msg) == (char *)msg) {
		errprintf("Bogus status message: msg_data finds no host.test. Sent from: '%s', data:'%s'\n",
			  sender, msg);
		return;
	}

	issummary = (log->host->hosttype == H_SUMMARY);

	if (strncmp(msg, "status+", 7) == 0) {
		validity = durationvalue(msg+7);
	}

	if (log->enabletime == DISABLED_UNTIL_OK) {
		/* The test is disabled until we get an OK status */
		if ((newcolor != COL_BLUE) && (decide_alertstate(newcolor) == A_OK)) {
			/* It's OK now - clear the disable status */
			log->enabletime = 0;
			if (log->dismsg) { xfree(log->dismsg); log->dismsg = NULL; }
			posttochannel(enadischn, channelnames[C_ENADIS], msg, sender, log->host->hostname, log, NULL);
		}
		else {
			/* Still not OK - keep it BLUE */
			newcolor = COL_BLUE;
		}
	}
	else if (log->enabletime > now) {
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
	if (log->enabletime) {
		if (log->enabletime == DISABLED_UNTIL_OK) log->validtime = INT_MAX;
		else if (log->enabletime > log->validtime) log->validtime = log->enabletime;
	}

	strncpy(log->sender, sender, sizeof(log->sender)-1);
	*(log->sender + sizeof(log->sender) - 1) = '\0';
	log->oldcolor = log->color;
	log->color = newcolor;
	oldalertstatus = decide_alertstate(log->oldcolor);
	newalertstatus = decide_alertstate(newcolor);
	if (log->grouplist) xfree(log->grouplist);
	if (grouplist) log->grouplist = strdup(grouplist);

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

			clear_cookie(log);

			/* Need to ensure that cookies are unique, hence the loop */
			log->cookie = -1; log->cookieexpires = 0;
			do {
				newcookie = (random() % 1000000);
			} while (find_cookie(newcookie));

			log->cookie = newcookie;
			rbtInsert(rbcookies, (void *)newcookie, log);

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
		/* Not alert state, so clear any cookies */
		if (log->cookie >= 0) clear_cookie(log);
	}

	if (!issummary && (!log->histsynced || (log->oldcolor != newcolor))) {
		/*
		 * Change of color goes to the status-change channel.
		 */
		dbgprintf("posting to stachg channel: host=%s, test=%s\n", hostname, testname);
		posttochannel(stachgchn, channelnames[C_STACHG], msg, sender, hostname, log, NULL);
		log->histsynced = 1;

		/*
		 * Dont update the log->lastchange timestamp while DOWNTIME is active.
		 * (It is only seen as active if the color has been forced BLUE).
		 */
		if (!log->downtimeactive && (log->oldcolor != newcolor)) {
			if (log->host->clientmsg && !log->host->clientmsgposted && (newalertstatus == A_ALERT) && log->test->clientsave) {
				posttochannel(clichgchn, channelnames[C_CLICHG], msg, sender, 
						hostname, log, NULL);
			}
			log->lastchange = getcurrenttime(NULL);
		}
	}

	if (!issummary) {
		if (newalertstatus == A_ALERT) {
			/* Status is critical, send alerts */
			dbgprintf("posting alert to page channel\n");

			log->activealert = 1;
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, log, NULL);
		}
		else if (log->activealert && (oldalertstatus != A_OK) && (newalertstatus == A_OK)) {
			/* Status has recovered, send recovery notice */
			dbgprintf("posting recovery to page channel\n");

			log->activealert = 0;
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, log, NULL);
		}
		else if (log->activealert && (log->oldcolor != newcolor)) {
			/* 
			 * Status is in-between critical and recovered, but we do have an
			 * active alert for this status. So tell the pager module that the
			 * color has changed.
			 */
			dbgprintf("posting color change to page channel\n");
			posttochannel(pagechn, channelnames[C_PAGE], msg, sender, hostname, log, NULL);
		}
	}

	dbgprintf("posting to status channel\n");
	posttochannel(statuschn, channelnames[C_STATUS], msg, sender, hostname, log, NULL);

	dbgprintf("<-handle_status\n");
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

	dbgprintf("-> handle_meta\n");

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

	dbgprintf("<- handle_meta\n");
}

void handle_data(char *msg, char *sender, char *origin, char *hostname, char *testname)
{
	void *hi;
	char *chnbuf;
	int buflen = 0;
	char *classname, *pagepath;

	dbgprintf("->handle_data\n");

	hi = hostinfo(hostname);
	classname = (hi ? bbh_item(hi, BBH_CLASS) : NULL);
	pagepath = (hi ? bbh_item(hi, BBH_ALLPAGEPATHS) : "");

	if (origin) buflen += strlen(origin); else dbgprintf("   origin is NULL\n");
	if (hostname) buflen += strlen(hostname); else dbgprintf("  hostname is NULL\n");
	if (testname) buflen += strlen(testname); else dbgprintf("  testname is NULL\n");
	if (msg) buflen += strlen(msg); else dbgprintf("  msg is NULL\n");
	if (classname) buflen += strlen(classname);
	if (pagepath) buflen += strlen(pagepath);
	buflen += 4;

	chnbuf = (char *)malloc(buflen);
	snprintf(chnbuf, buflen, "%s|%s|%s|%s|%s\n%s",
		 (origin ? origin : ""), 
		 (hostname ? hostname : ""), 
		 (testname ? testname : ""), 
		 (classname ? classname : ""),
		 (pagepath ? pagepath : ""),
		 msg);

	posttochannel(datachn, channelnames[C_DATA], msg, sender, hostname, NULL, chnbuf);
	xfree(chnbuf);
	dbgprintf("<-handle_data\n");
}

void handle_notes(char *msg, char *sender, char *hostname)
{
	dbgprintf("->handle_notes\n");
	posttochannel(noteschn, channelnames[C_NOTES], msg, sender, hostname, NULL, NULL);
	dbgprintf("<-handle_notes\n");
}

void handle_usermsg(char *msg, char *sender, char *hostname)
{
	dbgprintf("->handle_usermsg\n");
	posttochannel(userchn, channelnames[C_USER], msg, sender, hostname, NULL, NULL);
	dbgprintf("<-handle_usermsg\n");
}

void handle_enadis(int enabled, conn_t *msg, char *sender)
{
	char *firstline = NULL, *hosttest = NULL, *durstr = NULL, *txtstart = NULL;
	char *hname = NULL, *tname = NULL;
	time_t expires = 0;
	int alltests = 0;
	RbtIterator hosthandle, testhandle;
	hobbitd_hostlist_t *hwalk = NULL;
	testinfo_t *twalk = NULL;
	hobbitd_log_t *log;
	char *p;
	char hostip[IP_ADDR_STRLEN];

	dbgprintf("->handle_enadis\n");

	MEMDEFINE(hostip);

	p = strchr(msg->buf, '\n'); if (p) *p = '\0';
	firstline = strdup(msg->buf);
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
			if (strcmp(durstr, "-1") == 0) {
				expires = DISABLED_UNTIL_OK;
			}
			else {
				expires = 60*durationvalue(durstr) + getcurrenttime(NULL);
			}

			txtstart = msg->buf + (durstr + strlen(durstr) - firstline);
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

	if (!oksender(maintsenders, hwalk->ip, msg->addr.sin_addr, msg->buf)) goto done;

	if (tname) {
		testhandle = rbtFind(rbtests, tname);
		if (testhandle == rbtEnd(rbtests)) {
			/* Unknown test */
			goto done;
		}
		else twalk = gettreeitem(rbtests, testhandle);
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
				posttochannel(enadischn, channelnames[C_ENADIS], msg->buf, sender, log->host->hostname, log, NULL);
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
				posttochannel(enadischn, channelnames[C_ENADIS], msg->buf, sender, log->host->hostname, log, NULL);
			}
		}
	}
	else {
		/* disable code goes here */

		if (alltests) {
			for (log = hwalk->logs; (log); log = log->next) {
				log->enabletime = expires;
				log->validtime = (expires == DISABLED_UNTIL_OK) ? INT_MAX : log->validtime;
				if (txtstart) {
					if (log->dismsg) xfree(log->dismsg);
					log->dismsg = strdup(txtstart);
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg->buf, sender, log->host->hostname, log, NULL);
				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->name, log->grouplist, log, COL_BLUE, NULL);
			}
		}
		else {
			for (log = hwalk->logs; (log && (log->test != twalk)); log = log->next) ;
			if (log) {
				log->enabletime = expires;
				log->validtime = (expires == DISABLED_UNTIL_OK) ? INT_MAX : log->validtime;
				if (txtstart) {
					if (log->dismsg) xfree(log->dismsg);
					log->dismsg = strdup(txtstart);
				}
				posttochannel(enadischn, channelnames[C_ENADIS], msg->buf, sender, log->host->hostname, log, NULL);

				/* Trigger an immediate status update */
				handle_status(log->message, sender, log->host->hostname, log->test->name, log->grouplist, log, COL_BLUE, NULL);
			}
		}

	}

done:
	MEMUNDEFINE(hostip);
	xfree(firstline);

	dbgprintf("<-handle_enadis\n");

	return;
}


void handle_ack(char *msg, char *sender, hobbitd_log_t *log, int duration)
{
	char *p;

	dbgprintf("->handle_ack\n");

	log->acktime = getcurrenttime(NULL)+duration*60;
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

	dbgprintf("<-handle_ack\n");
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

		dbgprintf("Got ackinfo: Level=%d,until=%d,ackby=%s,msg=%s\n", level, validuntil, ackedby, ackmsg);

		/* See if we already have this ack in the list */
		for (newack = log->acklist; (newack && ((level != newack->level) || strcmp(newack->ackedby, ackedby))); newack = newack->next);

		isnew = (newack == NULL);
		dbgprintf("This ackinfo is %s\n", (isnew ? "new" : "old"));
		if (isnew) {
			dbgprintf("Creating new ackinfo record\n");
			newack = (ackinfo_t *)malloc(sizeof(ackinfo_t));
		}
		else {
			/* Drop the old data so we dont leak memory */
			dbgprintf("Dropping old ackinfo data: From %s, msg=%s\n", newack->ackedby, newack->msg);
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

		if (ackinfologfd) {
			char timestamp[25];

			strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&received));
			fprintf(ackinfologfd, "%s %s %s %s %d %d %d %d %s\n",
				timestamp, log->host->hostname, log->test->name,
				newack->ackedby, newack->level, 
				(int)log->lastchange, (int)newack->received, (int)newack->validuntil, 
				nlencode(newack->msg));
			fflush(ackinfologfd);
		}
	}
	else {
		if (ackedby) xfree(ackedby);
		if (ackmsg) xfree(ackmsg);
	}
}

void handle_notify(char *msg, char *sender, char *hostname, char *testname)
{
	char *msgtext, *channelmsg;
	void *hi;

	dbgprintf("-> handle_notify\n");

	hi = hostinfo(hostname);

	msgtext = msg_data(msg);
	channelmsg = (char *)malloc(1024 + strlen(msgtext));

	/* Tell the pagers */
	sprintf(channelmsg, "%s|%s|%s\n%s", 
		hostname, (testname ? testname : ""), (hi ? bbh_item(hi, BBH_PAGEPATH) : ""), msgtext);
	posttochannel(pagechn, "notify", msg, sender, hostname, NULL, channelmsg);

	xfree(channelmsg);

	dbgprintf("<- handle_notify\n");
	return;
}

void handle_client(char *msg, char *sender, char *hostname, char *clientos, char *clientclass)
{
	char *chnbuf, *theclass;
	int msglen, buflen = 0;
	RbtIterator hosthandle;

	dbgprintf("->handle_client\n");

	/* Default class is the OS */
	theclass = (clientclass ? clientclass : clientos);
	buflen += strlen(hostname) + strlen(clientos) + strlen(theclass);
	if (msg) { msglen = strlen(msg); buflen += msglen; } else { dbgprintf("  msg is NULL\n"); return; }
	buflen += 5;

	if (clientsavemem) {
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
			hwalk->clientmsgtstamp = getcurrenttime(NULL);
			hwalk->clientmsgposted = 0;
		}
	}

	chnbuf = (char *)malloc(buflen);
	snprintf(chnbuf, buflen, "%s|%s|%s\n%s", hostname, clientos, theclass, msg);
	posttochannel(clientchn, channelnames[C_CLIENT], msg, sender, hostname, NULL, chnbuf);
	xfree(chnbuf);
	dbgprintf("<-handle_client\n");
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
	static strbuffer_t *res = NULL;
	ackinfo_t *awalk;
	char tmpstr[512];

	if (log->acklist == NULL) return NULL;

	if (res) clearstrbuffer(res); else res = newstrbuffer(0);

	for (awalk = log->acklist; (awalk); awalk = awalk->next) {
		if ((level != -1) && (awalk->level != level)) continue;
		snprintf(tmpstr, sizeof(tmpstr), "%d:%d:%d:%s:%s\n", 
			 (int)awalk->received, (int)awalk->validuntil, 
			 awalk->level, awalk->ackedby, awalk->msg);
		tmpstr[sizeof(tmpstr)-1] = '\0';
		addtobuffer(res, tmpstr);
	}

	return STRBUF(res);
}

void free_log_t(hobbitd_log_t *zombie)
{
	hobbitd_meta_t *mwalk, *mtmp;

	dbgprintf("-> free_log_t\n");
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
	if (zombie->grouplist) xfree(zombie->grouplist);
	flush_acklist(zombie, 1);
	xfree(zombie);
	dbgprintf("<- free_log_t\n");
}

void handle_dropnrename(enum droprencmd_t cmd, char *sender, char *hostname, char *n1, char *n2)
{
	char hostip[IP_ADDR_STRLEN];
	RbtIterator hosthandle, testhandle;
	hobbitd_hostlist_t *hwalk;
	testinfo_t *twalk, *newt;
	hobbitd_log_t *lwalk;
	char *marker = NULL;
	char *canonhostname;

	dbgprintf("-> handle_dropnrename\n");
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
		testhandle = rbtFind(rbtests, n1);
		if (testhandle == rbtEnd(rbtests)) goto done;
		twalk = gettreeitem(rbtests, testhandle);

		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next) ;
		if (lwalk == NULL) goto done;
		if (lwalk == hwalk->pinglog) hwalk->pinglog = NULL;
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
		testhandle = rbtFind(rbtests, n1);
		if (testhandle == rbtEnd(rbtests)) goto done;
		twalk = gettreeitem(rbtests, testhandle);

		for (lwalk = hwalk->logs; (lwalk && (lwalk->test != twalk)); lwalk = lwalk->next) ;
		if (lwalk == NULL) goto done;

		if (lwalk == hwalk->pinglog) hwalk->pinglog = NULL;

		testhandle = rbtFind(rbtests, n2);
		if (testhandle == rbtEnd(rbtests)) {
			newt = create_testinfo(n2);
		}
		else {
			newt = gettreeitem(rbtests, testhandle);
		}
		lwalk->test = newt;
		break;
	}

done:
	MEMUNDEFINE(hostip);

	dbgprintf("<- handle_dropnrename\n");

	return;
}


unsigned char *get_filecache(char *fn)
{
	RbtIterator handle;
	filecache_t *item;
	unsigned char *result;

	handle = rbtFind(rbfilecache, fn);
	if (handle == rbtEnd(rbfilecache)) return NULL;

	item = (filecache_t *)gettreeitem(rbfilecache, handle);
	if (item->len < 0) return NULL;

	result = (unsigned char *)malloc(item->len);
	memcpy(result, item->fdata, item->len);

	return result;
}


void add_filecache(char *fn, unsigned char *buf, off_t buflen)
{
	RbtIterator handle;
	filecache_t *newitem;

	handle = rbtFind(rbfilecache, fn);
	if (handle == rbtEnd(rbfilecache)) {
		newitem = (filecache_t *)malloc(sizeof(filecache_t));
		newitem->fn = strdup(fn);
		newitem->len = buflen;
		newitem->fdata = (unsigned char *)malloc(buflen);
		memcpy(newitem->fdata, buf, buflen);
		rbtInsert(rbfilecache, newitem->fn, newitem);
	}
	else {
		newitem = (filecache_t *)gettreeitem(rbfilecache, handle);
		if (newitem->fdata) xfree(newitem->fdata);
		newitem->len = buflen;
		newitem->fdata = (unsigned char *)malloc(buflen);
		memcpy(newitem->fdata, buf, buflen);
	}
}


void flush_filecache(void)
{
	RbtIterator handle;

	for (handle = rbtBegin(rbfilecache); (handle != rbtEnd(rbfilecache)); handle = rbtNext(rbfilecache, handle)) {
		filecache_t *item = (filecache_t *)gettreeitem(rbfilecache, handle);
		if (item->fdata) xfree(item->fdata);
		item->len = -1;
	}
}


int get_config(char *fn, conn_t *msg)
{
	char fullfn[PATH_MAX];
	FILE *fd = NULL;
	strbuffer_t *inbuf, *result;

	dbgprintf("-> get_config %s\n", fn);
	sprintf(fullfn, "%s/etc/%s", xgetenv("BBHOME"), fn);
	fd = stackfopen(fullfn, "r", NULL);
	if (fd == NULL) {
		errprintf("Config file %s not found\n", fn);
		return -1;
	}

	inbuf = newstrbuffer(0);
	result = newstrbuffer(0);
	while (stackfgets(inbuf, NULL) != NULL) addtostrbuffer(result, inbuf);
	stackfclose(fd);
	freestrbuffer(inbuf);

	msg->buflen = STRBUFLEN(result);
	msg->buf = grabstrbuffer(result);
	msg->bufp = msg->buf + msg->buflen;

	dbgprintf("<- get_config\n");

	return 0;
}

int get_binary(char *fn, conn_t *msg)
{
	char fullfn[PATH_MAX];
	int fd;
	struct stat st;
	unsigned char *result;

	dbgprintf("-> get_binary %s\n", fn);
	sprintf(fullfn, "%s/download/%s", xgetenv("BBHOME"), fn);

	result = get_filecache(fullfn);
	if (!result) {
		fd = open(fullfn, O_RDONLY);
		if (fd == -1) {
			errprintf("Download file %s not found\n", fn);
			return -1;
		}

		if (fstat(fd, &st) == 0) {
			ssize_t n;

			result = (unsigned char *)malloc(st.st_size);
			n = read(fd, result, st.st_size);
			if (n != st.st_size) {
				errprintf("Error reading from %s : %s\n", fn, strerror(errno));
				xfree(result); result = NULL;
				close(fd);
				return -1;
			}
		}

		add_filecache(fullfn, result, st.st_size);
	}

	msg->buflen = st.st_size;
	msg->buf = result;
	msg->bufp = msg->buf + msg->buflen;

	dbgprintf("<- get_binary\n");

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

void setup_filter(char *buf, char *defaultfields, 
		  pcre **spage, pcre **shost, pcre **snet, 
		  pcre **stest, int *scolor, int *acklevel, char **fields,
		  char **chspage, char **chshost, char **chsnet, char **chstest)
{
	char *tok, *s;
	int idx = 0;

	dbgprintf("-> setup_filter: %s\n", buf);

	*spage = *shost = *snet = *stest = NULL;
	if (chspage) *chspage = NULL;
	if (chshost) *chshost = NULL;
	if (chsnet)  *chsnet  = NULL;
	if (chstest) *chstest = NULL;
	*fields = NULL;
	*scolor = -1;

	tok = strtok(buf, " \t\r\n");
	if (tok) tok = strtok(NULL, " \t\r\n");
	while (tok) {
		/* Get filter */
		if (strncmp(tok, "page=", 5) == 0) {
			if (*(tok+5)) {
				*spage = compileregex(tok+5);
				if (chspage) *chspage = tok+5;
			}
		}
		else if (strncmp(tok, "host=", 5) == 0) {
			if (*(tok+5)) {
				*shost = compileregex(tok+5);
				if (chshost) *chshost = tok+5;
			}
		}
		else if (strncmp(tok, "net=", 4) == 0) {
			if (*(tok+4)) {
				*snet = compileregex(tok+4);
				if (chsnet) *chsnet = tok+4;
			}
		}
		else if (strncmp(tok, "test=", 5) == 0) {
			if (*(tok+5)) {
				*stest = compileregex(tok+5);
				if (chstest) *chstest = tok+5;
			}
		}
		else if (strncmp(tok, "fields=", 7) == 0) *fields = tok+7;
		else if (strncmp(tok, "color=", 6) == 0) *scolor = colorset(tok+6, 0);
		else if (strncmp(tok, "acklevel=", 9) == 0) *acklevel = atoi(tok+9);
		else {
			/* Might be an old-style HOST.TEST request */
			char *hname, *tname, hostip[IP_ADDR_STRLEN];
			char *hnameexp, *tnameexp;

			MEMDEFINE(hostip);

			hname = tok;
			tname = strrchr(tok, '.');
			if (tname) { *tname = '\0'; tname++; }
			s = hname; while ((s = strchr(s, ',')) != NULL) *s = '.';
			hname = knownhost(hname, hostip, ghosthandling);

			if (hname && tname) {
				hnameexp = (char *)malloc(strlen(hname)+3);
				sprintf(hnameexp, "^%s$", hname);
				*shost = compileregex(hnameexp);
				xfree(hnameexp);

				tnameexp = (char *)malloc(strlen(tname)+3);
				sprintf(tnameexp, "^%s$", tname);
				*stest = compileregex(tnameexp);
				xfree(tnameexp);

				if (chshost) *chshost = hname;
				if (chstest) *chstest = tname;
			}
		}

		tok = strtok(NULL, " \t\r\n");
	}

	/* If no fields given, provide the default set. */
	if (*fields == NULL) *fields = defaultfields;

	s = strdup(*fields);
	tok = strtok(s, ",");
	while (tok) {
		enum boardfield_t fieldid = F_LAST;
		enum bbh_item_t bbhfieldid = BBH_LAST;
		int validfield = 1;

		if (strncmp(tok, "BBH_", 4) == 0) {
			fieldid = F_HOSTINFO;
			bbhfieldid = bbh_key_idx(tok);
			validfield = (bbhfieldid != BBH_LAST);
		}
		else {
			int i;
			for (i=0; (boardfieldnames[i].name && strcmp(tok, boardfieldnames[i].name)); i++) ;
			if (boardfieldnames[i].name) {
				fieldid = boardfieldnames[i].id;
				bbhfieldid = BBH_LAST;
			}
		}

		if ((fieldid != F_LAST) && (idx < BOARDFIELDS_MAX) && validfield) {
			boardfields[idx].field = fieldid;
			boardfields[idx].bbhfield = bbhfieldid;
			idx++;
		}

		tok = strtok(NULL, ",");
	}
	boardfields[idx].field = F_NONE;
	boardfields[idx].bbhfield = BBH_LAST;

	xfree(s);

	dbgprintf("<- setup_filter: %s\n", buf);
}

int match_host_filter(void *hinfo, pcre *spage, pcre *shost, pcre *snet)
{
	char *match;

	match = bbh_item(hinfo, BBH_HOSTNAME);
	if (shost && match && !matchregex(match, shost)) return 0;
	if (spage) {
		int matchres = 0;

		match = bbh_item_multi(hinfo, BBH_PAGEPATH);
		while (match && (matchres == 0)) {
			if (match && matchregex(match, spage)) matchres = 1;
			match = bbh_item_multi(NULL, BBH_PAGEPATH);
		}

		if (matchres == 0) return 0;
	}
	match = bbh_item(hinfo, BBH_NET);
	if (snet  && match && !matchregex(match, snet))  return 0;

	return 1;
}

int match_test_filter(hobbitd_log_t *log, pcre *stest, int scolor)
{
	/* Testname filter */
	if (stest && !matchregex(log->test->name, stest)) return 0;

	/* Color filter */
	if ((scolor != -1) && (((1 << log->color) & scolor) == 0)) return 0;

	return 1;
}



void generate_outbuf(char **outbuf, char **outpos, int *outsz, 
		     hobbitd_hostlist_t *hwalk, hobbitd_log_t *lwalk, int acklevel)
{
	int f_idx;
	char *buf, *bufp;
	int bufsz;
	char *eoln;
	void *hinfo = NULL;
	char *acklist = NULL;
	int needed, used;
	enum boardfield_t f_type;

	buf = *outbuf;
	bufp = *outpos;
	bufsz = *outsz;
	needed = 1024;

	/* First calculate how much buffer space is needed */
	for (f_idx = 0, f_type = boardfields[0].field; ((f_type != F_NONE) && (f_type != F_LAST)); f_type = boardfields[++f_idx].field) {
		if ((lwalk == NULL) && (f_type != F_HOSTINFO)) continue;

		switch (f_type) {
		  case F_ACKMSG: if (lwalk->ackmsg) needed += 2*strlen(lwalk->ackmsg); break;
		  case F_DISMSG: if (lwalk->dismsg) needed += 2*strlen(lwalk->dismsg); break;
		  case F_LINE1:
		  case F_MSG: needed += 2*strlen(lwalk->message); break;

		  case F_ACKLIST:
			flush_acklist(lwalk, 0);
			acklist = acklist_string(lwalk, acklevel);
			if (acklist) needed += 2*strlen(acklist);
			break;

		  case F_HOSTINFO:
			if (!hinfo) hinfo = hostinfo(hwalk->hostname);
			if (hinfo) {
				char *infostr = bbh_item(hinfo, boardfields[f_idx].bbhfield);
				if (infostr) needed += strlen(infostr);
			}
			break;

		  default: break;
		}
	}

	/* Make sure the buffer is large enough */
	used = (bufp - buf);
	if ((bufsz - used) < needed) {
		bufsz += needed;
		buf = (char *)realloc(buf, bufsz);
		bufp = buf + used;
	}

	/* Now generate the data */
	for (f_idx = 0, f_type = boardfields[0].field; ((f_type != F_NONE) && (f_type != F_LAST)); f_type = boardfields[++f_idx].field) {
		if ((lwalk == NULL) && (f_type != F_HOSTINFO)) continue;
		if (f_idx > 0) bufp += sprintf(bufp, "|");

		switch (f_type) {
		  case F_NONE: break;
		  case F_HOSTNAME: bufp += sprintf(bufp, "%s", hwalk->hostname); break;
		  case F_TESTNAME: bufp += sprintf(bufp, "%s", lwalk->test->name); break;
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
		  case F_CLIENTTSTAMP: bufp += sprintf(bufp, "%ld", (hwalk->clientmsg ? (long) hwalk->clientmsgtstamp : 0)); break;
		  case F_ACKLIST: if (acklist) bufp += sprintf(bufp, "%s", nlencode(acklist)); break;

		  case F_HOSTINFO:
			if (hinfo) {	/* hinfo has been set above while scanning for the needed bufsize */
				char *infostr = bbh_item(hinfo, boardfields[f_idx].bbhfield);
				if (infostr) bufp += sprintf(bufp, "%s", infostr);
			}
			break;

		  case F_LAST: break;
		}
	}
	bufp += sprintf(bufp, "\n");

	*outbuf = buf;
	*outpos = bufp;
	*outsz = bufsz;
}


void generate_hostinfo_outbuf(char **outbuf, char **outpos, int *outsz, void *hinfo)
{
	int f_idx;
	char *buf, *bufp;
	int bufsz;
	char *infostr = NULL;

	buf = *outbuf;
	bufp = *outpos;
	bufsz = *outsz;

	for (f_idx = 0; (boardfields[f_idx].field != F_NONE); f_idx++) {
		int needed = 1024;
		int used = (bufp - buf);

		switch (boardfields[f_idx].field) {
		  case F_HOSTINFO:
			infostr = bbh_item(hinfo, boardfields[f_idx].bbhfield);
			if (infostr) needed += strlen(infostr);
			break;

		  default: break;
		}

		if ((bufsz - used) < needed) {
			bufsz += 4096 + needed;
			buf = (char *)realloc(buf, bufsz);
			bufp = buf + used;
		}

		if (f_idx > 0) bufp += sprintf(bufp, "|");

		switch (boardfields[f_idx].field) {
		  case F_HOSTINFO: if (infostr) bufp += sprintf(bufp, "%s", infostr); break;
		  default: break;
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
	testinfo_t *t;
	hobbitd_log_t *log;
	int color;
	char *downcause;
	char sender[IP_ADDR_STRLEN];
	char *grouplist;
	time_t now;
	char *msgfrom;

	nesting++;
	if (debug) {
		char *eoln = strchr(msg->buf, '\n');

		if (eoln) *eoln = '\0';
		dbgprintf("-> do_message/%d (%d bytes): %s\n", nesting, msg->buflen, msg->buf);
		if (eoln) *eoln = '\n';
	}

	MEMDEFINE(sender);

	/* Most likely, we will not send a response */
	msg->doingwhat = NOTALK;
	strncpy(sender, inet_ntoa(msg->addr.sin_addr), sizeof(sender));
	now = getcurrenttime(NULL);

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
				get_hts(currmsg, sender, origin, &h, &t, &grouplist, &log, &color, &downcause, NULL, 0, 0);
				if (!oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, currmsg)) validsender = 0;
			}

			if (validsender) {
				get_hts(currmsg, sender, origin, &h, &t, &grouplist, &log, &color, &downcause, NULL, 1, 1);
				if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
					fprintf(dbgfd, "\n---- combo message from %s ----\n%s---- end message ----\n", sender, currmsg);
					fflush(dbgfd);
				}

				if (color == COL_PURPLE) {
					errprintf("Ignored PURPLE status update from %s for %s.%s\n",
						  sender, (h ? h->hostname : "<unknown>"), (t ? t->name : "unknown"));
				}
				else {
					/* Count individual status-messages also */
					update_statistics(currmsg);

					if (h && t && log && (color != -1)) {
						handle_status(currmsg, sender, h->hostname, t->name, grouplist, log, color, downcause);
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

			get_hts(currmsg, sender, origin, &h, &t, NULL, &log, &color, NULL, NULL, 0, 0);
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
			get_hts(msg->buf, sender, origin, &h, &t, &grouplist, &log, &color, &downcause, NULL, 0, 0);
			if (!oksender(statussenders, (h ? h->ip : NULL), msg->addr.sin_addr, msg->buf)) goto done;
		}

		get_hts(msg->buf, sender, origin, &h, &t, &grouplist, &log, &color, &downcause, NULL, 1, 1);
		if (h && dbgfd && dbghost && (strcasecmp(h->hostname, dbghost) == 0)) {
			fprintf(dbgfd, "\n---- status message from %s ----\n%s---- end message ----\n", sender, msg->buf);
			fflush(dbgfd);
		}

		if (color == COL_PURPLE) {
			errprintf("Ignored PURPLE status update from %s for %s.%s\n",
				  sender, (h ? h->hostname : "<unknown>"), (t ? t->name : "unknown"));
		}
		else {
			if (h && t && log && (color != -1)) {
				handle_status(msg->buf, sender, h->hostname, t->name, grouplist, log, color, downcause);
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
			char *hname, hostip[IP_ADDR_STRLEN];

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
		get_hts(msg->buf, sender, origin, &h, &t, NULL, &log, &color, NULL, NULL, 1, 1);
		if (h && t && log && (color != -1)) {
			handle_status(msg->buf, sender, h->hostname, t->name, NULL, log, color, NULL);
		}
	}
	else if ((strncmp(msg->buf, "notes", 5) == 0) || (strncmp(msg->buf, "usermsg", 7) == 0)) {
		char *hostname, *bhost, *ehost, *p;
		char savechar;

		bhost = msg->buf + strcspn(msg->buf, " \t\r\n"); bhost += strspn(bhost, " \t");
		ehost = bhost + strcspn(bhost, " \t\r\n");
		savechar = *ehost; *ehost = '\0';
		hostname = strdup(bhost);
		*ehost = savechar;

		p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';
		if (*hostname == '\0') { errprintf("Invalid notes/user message from %s - blank hostname\n", sender); xfree(hostname); hostname = NULL; }

		if (hostname) {
			char *hname, hostip[IP_ADDR_STRLEN];

			MEMDEFINE(hostip);

			hname = knownhost(hostname, hostip, ghosthandling);
			if (hname == NULL) {
				log_ghost(hostname, sender, msg->buf);
			}
			else {
				if (*msg->buf == 'n') {
					/* "notes" message */
					if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) {
						/* Invalid sender */
						errprintf("Invalid notes message - sender %s not allowed for host %s\n", sender, hostname);
					}
					else {
						handle_notes(msg->buf, sender, hostname);
					}
				}
				else if (*msg->buf == 'u') {
					/* "usermsg" message */
					if (!oksender(statussenders, NULL, msg->addr.sin_addr, msg->buf)) {
						/* Invalid sender */
						errprintf("Invalid user message - sender %s not allowed for host %s\n", sender, hostname);
					}
					else {
						handle_usermsg(msg->buf, sender, hostname);
					}
				}
			}

			xfree(hostname);

			MEMUNDEFINE(hostip);
		}
	}
	else if (strncmp(msg->buf, "enable", 6) == 0) {
		handle_enadis(1, msg, sender);
	}
	else if (strncmp(msg->buf, "disable", 7) == 0) {
		handle_enadis(0, msg, sender);
	}
	else if (allow_downloads && (strncmp(msg->buf, "config", 6) == 0)) {
		char *conffn, *p;

		if (!oksender(statussenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		p = msg->buf + 6; p += strspn(p, " \t");
		p = strtok(p, " \t\r\n");
		conffn = strdup(p);
		xfree(msg->buf);
		if (conffn && (strstr(conffn, "../") == NULL) && (get_config(conffn, msg) == 0) ) {
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf;
		}
	}
	else if (allow_downloads && (strncmp(msg->buf, "download", 8) == 0)) {
		char *fn, *p;

		if (!oksender(statussenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		p = msg->buf + 8; p += strspn(p, " \t");
		p = strtok(p, " \t\r\n");
		fn = strdup(p);
		xfree(msg->buf);
		if (fn && (strstr(fn, "../") == NULL) && (get_binary(fn, msg) == 0) ) {
			msg->doingwhat = RESPONDING;
			msg->bufp = msg->buf;
		}
	}
	else if (strncmp(msg->buf, "flush filecache", 15) == 0) {
		flush_filecache();
	}
	else if (strncmp(msg->buf, "query ", 6) == 0) {
		get_hts(msg->buf, sender, origin, &h, &t, NULL, &log, &color, NULL, NULL, 0, 0);
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

		pcre *spage = NULL, *shost = NULL, *snet = NULL, *stest = NULL;
		char *chspage, *chshost, *chsnet, *chstest, *fields = NULL;
		int scolor = -1, acklevel = -1;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf, 
		 	     "hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,ackmsg,dismsg,client",
			     &spage, &shost, &snet, &stest, &scolor, &acklevel, &fields,
			     &chspage, &chshost, &chsnet, &chstest);

		log = find_log(chshost, chstest, "", &h);
		if (log) {
			char *buf, *bufp;
			int bufsz;

			flush_acklist(log, 0);
			if (log->message == NULL) {
				errprintf("%s.%s has a NULL message\n", log->host->hostname, log->test->name);
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

		freeregex(spage); freeregex(shost); freeregex(snet); freeregex(stest);
	}
	else if (strncmp(msg->buf, "hobbitdxlog ", 12) == 0) {
		/* 
		 * Request for a single status log in XML format
		 * hobbitdxlog HOST.TEST
		 *
		 */
		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		get_hts(msg->buf, sender, origin, &h, &t, NULL, &log, &color, NULL, NULL, 0, 0);
		if (log) {
			char *buf, *bufp;
			int bufsz, buflen;
			hobbitd_meta_t *mwalk;

			flush_acklist(log, 0);
			if (log->message == NULL) {
				errprintf("%s.%s has a NULL message\n", log->host->hostname, log->test->name);
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
			bufp += sprintf(bufp, "  <Type>%s</Type>\n", log->test->name);
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
		testinfo_t trendstest, infotest;
		char *buf, *bufp;
		int bufsz;
		pcre *spage = NULL, *shost = NULL, *snet = NULL, *stest = NULL;
		char *chspage = NULL, *chshost = NULL, *chsnet = NULL, *chstest = NULL;
		char *fields = NULL;
		int scolor = -1, acklevel = -1;
		static unsigned int lastboardsize = 0;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf, 
			     "hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,line1",
			     &spage, &shost, &snet, &stest, &scolor, &acklevel, &fields,
			     &chspage, &chshost, &chsnet, &chstest);

		if (lastboardsize <= 8192) {
			/* A guesstimate - 8 tests per hosts, 1KB/test (only 1st line of msg) */
			bufsz = (hostcount+1)*8*1024; 
		}
		else {
			/* Add 10% to the last size we used */
			bufsz = lastboardsize + (lastboardsize / 10);
		}
		bufp = buf = (char *)malloc(bufsz);

		/* Setup fake log-records for the "info" and "trends" data. */
		memset(&infotest, 0, sizeof(infotest));
		infotest.name = xgetenv("INFOCOLUMN");
		memset(&infologrec, 0, sizeof(infologrec));
		infologrec.test = &infotest;

		memset(&trendstest, 0, sizeof(trendstest));
		trendstest.name = xgetenv("TRENDSCOLUMN");
		memset(&rrdlogrec, 0, sizeof(rrdlogrec));
		rrdlogrec.test = &trendstest;

		infologrec.color = rrdlogrec.color = COL_GREEN;
		infologrec.message = rrdlogrec.message = "";

		for (hosthandle = rbtBegin(rbhosts); (hosthandle != rbtEnd(rbhosts)); hosthandle = rbtNext(rbhosts, hosthandle)) {
			hwalk = gettreeitem(rbhosts, hosthandle);
			if (!hwalk) {
				errprintf("host-tree has a record with no data\n");
				continue;
			}

			/* If there is a hostname filter, drop the "summary" 'hosts' */
			if (shost && (hwalk->hosttype != H_NORMAL)) continue;

			firstlog = hwalk->logs;

			if (hwalk->hosttype == H_NORMAL) {
				void *hinfo = hostinfo(hwalk->hostname);

				if (!hinfo) {
					errprintf("Hostname '%s' in tree, but no host-info\n", hwalk->hostname);
					continue;
				}

				/* Host/pagename filter */
				if (!match_host_filter(hinfo, spage, shost, snet)) continue;

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
				if (!match_test_filter(lwalk, stest, scolor)) continue;

				if (lwalk->message == NULL) {
					errprintf("%s.%s has a NULL message\n", lwalk->host->hostname, lwalk->test->name);
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

		freeregex(spage); freeregex(shost); freeregex(snet); freeregex(stest);
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
		pcre *spage = NULL, *shost = NULL, *snet = NULL, *stest = NULL;
		char *chspage = NULL, *chshost = NULL, *chsnet = NULL, *chstest = NULL;
		char *fields = NULL;
		int scolor = -1, acklevel = -1;
		static unsigned int lastboardsize = 0;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf,
			     "hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,line1",
			     &spage, &shost, &snet, &stest, &scolor, &acklevel, &fields,
			     &chspage, &chshost, &chsnet, &chstest);

		if (lastboardsize <= 8192) {
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
			void *hinfo;

			hwalk = gettreeitem(rbhosts, hosthandle);
			if (!hwalk) {
				errprintf("host-tree has a record with no data\n");
				continue;
			}

			hinfo = hostinfo(hwalk->hostname);

			/* Host/pagename filter */
			if (!match_host_filter(hinfo, spage, shost, snet)) continue;

			for (lwalk = hwalk->logs; (lwalk); lwalk = lwalk->next) {
				char *eoln;
				int buflen = (bufp - buf);

				if (!match_test_filter(lwalk, stest, scolor)) continue;

				if (lwalk->message == NULL) {
					errprintf("%s.%s has a NULL message\n", lwalk->host->hostname, lwalk->test->name);
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
				bufp += sprintf(bufp, "    <Type>%s</Type>\n", lwalk->test->name);
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

		freeregex(spage); freeregex(shost); freeregex(snet); freeregex(stest);
	}
	else if (strncmp(msg->buf, "hostinfo", 8) == 0) {
		/* 
		 * Request for host configuration info
		 *
		 */
		void *hinfo;
		char *buf, *bufp;
		int bufsz;
		pcre *spage = NULL, *shost = NULL, *stest = NULL, *snet = NULL;
		char *chspage = NULL, *chshost = NULL, *chsnet = NULL, *chstest = NULL;
		char *fields = NULL;
		int scolor = -1, acklevel = -1;
		static unsigned int lastboardsize = 0;

		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		setup_filter(msg->buf, 
			     "BBH_HOSTNAME,BBH_IP,BBH_RAW",
			     &spage, &shost, &snet, &stest, &scolor, &acklevel, &fields,
			     &chspage, &chshost, &chsnet, &chstest);

		if (lastboardsize == 0) {
			/* A guesstimate - 500 bytes per host */
			bufsz = (hostcount+1)*500;
		}
		else {
			/* Add 10% to the last size we used */
			bufsz = lastboardsize + (lastboardsize / 10);
		}
		bufp = buf = (char *)malloc(bufsz);

		for (hinfo = first_host(); (hinfo); hinfo = next_host(hinfo, 0)) {
			if (!match_host_filter(hinfo, spage, shost, snet)) continue;
			generate_hostinfo_outbuf(&buf, &bufp, &bufsz, hinfo);
		}

		*bufp = '\0';

		xfree(msg->buf);
		msg->doingwhat = RESPONDING;
		msg->bufp = msg->buf = buf;
		msg->buflen = (bufp - buf);
		if (msg->buflen > lastboardsize) lastboardsize = msg->buflen;

		freeregex(spage); freeregex(shost); freeregex(snet); freeregex(stest);
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

		get_hts(msg->buf, sender, origin, &h, &t, NULL, &log, &color, NULL, &ackall, 0, 0);
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
	else if (strncmp(msg->buf, "ping", 4) == 0) {
		/* Tell them we're here */
		char id[128];

		sprintf(id, "hobbitd %s\n", VERSION);
		msg->doingwhat = RESPONDING;
		xfree(msg->buf);
		msg->bufp = msg->buf = strdup(id);
		msg->buflen = strlen(msg->buf);
	}
	else if (strncmp(msg->buf, "notify", 6) == 0) {
		if (!oksender(maintsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;
		get_hts(msg->buf, sender, origin, &h, &t, NULL, &log, &color, NULL, NULL, 0, 0);
		if (h && t) handle_notify(msg->buf, sender, h->hostname, t->name);
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
		/* "client HOSTNAME.CLIENTOS CLIENTCLASS" */
		char *hostname = NULL, *clientos = NULL, *clientclass = NULL;
		char *hname = NULL;
		char *line1, *p;
		char savech;

		msgfrom = strstr(msg->buf, "\n[proxy]\n");
		if (msgfrom) {
			char *ipline = strstr(msgfrom, "\nClientIP:");
			if (ipline) { 
				sscanf(ipline, "\nClientIP:%16s\n", sender);
			}
		}

		p = msg->buf + strcspn(msg->buf, "\r\n");
		if ((*p == '\r') || (*p == '\n')) {
			savech = *p;
			*p = '\0';
		}
		else {
			p = NULL;
		}
		line1 = strdup(msg->buf); if (p) *p = savech;

		p = strtok(line1, " \t"); /* Skip the client keyword */
		if (p) hostname = strtok(NULL, " \t"); /* Actually, HOSTNAME.CLIENTOS */
		if (hostname) {
			clientos = strrchr(hostname, '.'); 
			if (clientos) { *clientos = '\0'; clientos++; }
			p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';
			clientclass = strtok(NULL, " \t");
		}

		if (hostname && clientos) {
			char hostip[IP_ADDR_STRLEN];

			MEMDEFINE(hostip);

			hname = knownhost(hostname, hostip, ghosthandling);

			if (hname == NULL) {
				log_ghost(hostname, sender, msg->buf);
			}
			else if (!oksender(statussenders, hostip, msg->addr.sin_addr, msg->buf)) {
				/* Invalid sender */
				errprintf("Invalid client message - sender %s not allowed for host %s\n", sender, hostname);
				hname = NULL;
			}
			else {
				void *hinfo = hostinfo(hname);

				handle_client(msg->buf, sender, hname, clientos, clientclass);

				if (hinfo) {
					if (clientos) bbh_set_item(hinfo, BBH_OS, clientos);
					if (clientclass) {
						/*
						 * If the client sends an explicit class,
						 * save it for later use unless there is an
						 * explicit override (BBH_CLASS is alread set).
						 */
						char *forcedclass = bbh_item(hinfo, BBH_CLASS);

						if (!forcedclass) 
							bbh_set_item(hinfo, BBH_CLASS, clientclass);
						else 
							clientclass = forcedclass;
					}
				}
			}

			MEMUNDEFINE(hostip);
		}

		if (hname) {
			char *cfg;
			
			cfg = get_clientconfig(hname, clientclass, clientos);
			if (cfg) {
				msg->doingwhat = RESPONDING;
				xfree(msg->buf);
				msg->bufp = msg->buf = strdup(cfg);
				msg->buflen = strlen(msg->buf);
			}
		}

		xfree(line1);
	}
	else if (strncmp(msg->buf, "clientlog ", 10) == 0) {
		char *hostname, *p;
		RbtIterator hosthandle;
		if (!oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) goto done;

		p = msg->buf + strlen("clientlog"); p += strspn(p, "\t ");
		hostname = p; p += strcspn(p, "\t "); if (*p) { *p = '\0'; p++; }

		hosthandle = rbtFind(rbhosts, hostname);
		if (hosthandle != rbtEnd(rbhosts)) {
			hobbitd_hostlist_t *hwalk;
			hwalk = gettreeitem(rbhosts, hosthandle);

			if (hwalk->clientmsg) {
				char *sections = NULL;

				if (strncmp(p, "section=", 8) == 0) sections = strdup(p+8);

				xfree(msg->buf);
				msg->buf = NULL;
				msg->doingwhat = RESPONDING;

				if (!sections) {
					msg->bufp = msg->buf = strdup(hwalk->clientmsg);
					msg->buflen = strlen(msg->buf);
				}
				else {
					char *onesect;
					strbuffer_t *resp;

					onesect = strtok(sections, ",");
					resp = newstrbuffer(0);
					while (onesect) {
						char *sectmarker = (char *)malloc(strlen(onesect) + 4);
						char *beginp, *endp;

						sprintf(sectmarker, "\n[%s]", onesect);
						beginp = strstr(hwalk->clientmsg, sectmarker);
						if (beginp) {
							beginp += 1; /* Skip the newline */
							endp = strstr(beginp, "\n[");
							if (endp) { endp++; *endp = '\0'; }
							addtobuffer(resp, beginp);
							if (endp) *endp = '[';
						}

						xfree(sectmarker);
						onesect = strtok(NULL, ",");
					}

					msg->buflen = STRBUFLEN(resp);
					msg->buf = grabstrbuffer(resp);
					if (!msg->buf) msg->buf = strdup("");
					msg->bufp = msg->buf;
				}
			}
		}
	}
	else if (strncmp(msg->buf, "ghostlist", 9) == 0) {
		if (oksender(wwwsenders, NULL, msg->addr.sin_addr, msg->buf)) {
			RbtHandle ghandle;
			ghostlist_t *gwalk;
			strbuffer_t *resp;
			char msgline[1024];

			resp = newstrbuffer(0);

			for (ghandle = rbtBegin(rbghosts); (ghandle != rbtEnd(rbghosts)); ghandle = rbtNext(rbghosts, ghandle)) {
				gwalk = (ghostlist_t *)gettreeitem(rbghosts, ghandle);
				snprintf(msgline, sizeof(msgline), "%s|%s|%ld\n", 
					 gwalk->name, gwalk->sender, (long int)gwalk->tstamp);
				addtobuffer(resp, msgline);
			}

			msg->doingwhat = RESPONDING;
			xfree(msg->buf);
			msg->buflen = STRBUFLEN(resp);
			msg->buf = grabstrbuffer(resp);
			if (!msg->buf) msg->buf = strdup("");
			msg->bufp = msg->buf;
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

	dbgprintf("<- do_message/%d\n", nesting);
	nesting--;
}


void save_checkpoint(void)
{
	char *tempfn;
	FILE *fd;
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk;
	hobbitd_log_t *lwalk;
	time_t now = getcurrenttime(NULL);
	scheduletask_t *swalk;
	ackinfo_t *awalk;
	int iores = 0;

	if (checkpointfn == NULL) return;

	dbgprintf("-> save_checkpoint\n");
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
			if (lwalk->dismsg && (lwalk->enabletime < now) && (lwalk->enabletime != DISABLED_UNTIL_OK)) {
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
				lwalk->origin, hwalk->hostname, lwalk->test->name, lwalk->sender,
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
				iores = fprintf(fd, "@@HOBBITDCHK-V1|.acklist.|%s|%s|%d|%d|%d|%d|%s|%s\n",
						hwalk->hostname, lwalk->test->name,
			 			(int)awalk->received, (int)awalk->validuntil, (int)awalk->cleartime,
						awalk->level, awalk->ackedby, awalk->msg);
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
	dbgprintf("<- save_checkpoint\n");
}


void load_checkpoint(char *fn)
{
	FILE *fd;
	strbuffer_t *inbuf;
	char *item;
	int i, err;
	char hostip[IP_ADDR_STRLEN];
	RbtIterator hosthandle, testhandle, originhandle;
	hobbitd_hostlist_t *hitem = NULL;
	testinfo_t *t = NULL;
	char *origin = NULL;
	hobbitd_log_t *ltail = NULL;
	char *originname, *hostname, *testname, *sender, *testflags, *statusmsg, *disablemsg, *ackmsg; 
	time_t logtime, lastchange, validtime, enabletime, acktime, cookieexpires;
	int color = COL_GREEN, oldcolor = COL_GREEN, cookie;
	int count = 0;

	fd = fopen(fn, "r");
	if (fd == NULL) {
		errprintf("Cannot access checkpoint file %s for restore\n", fn);
		return;
	}

	MEMDEFINE(hostip);

	inbuf = newstrbuffer(0);
	initfgets(fd);
	while (unlimfgets(inbuf, fd)) {
		originname = hostname = testname = sender = testflags = statusmsg = disablemsg = ackmsg = NULL;
		logtime = lastchange = validtime = enabletime = acktime = cookieexpires = 0;
		cookie = -1;
		err = 0;

		if (strncmp(STRBUF(inbuf), "@@HOBBITDCHK-V1|.task.|", 23) == 0) {
			scheduletask_t *newtask = (scheduletask_t *)calloc(1, sizeof(scheduletask_t));

			item = gettok(STRBUF(inbuf), "|\n"); i = 0;
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

			if (newtask->id && (newtask->executiontime > getcurrenttime(NULL)) && newtask->sender && newtask->command) {
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

		if (strncmp(STRBUF(inbuf), "@@HOBBITDCHK-V1|.acklist.|", 26) == 0) {
			hobbitd_log_t *log = NULL;
			ackinfo_t *newack = (ackinfo_t *)calloc(1, sizeof(ackinfo_t));

			hitem = NULL;

			item = gettok(STRBUF(inbuf), "|\n"); i = 0;
			while (item) {

				switch (i) {
				  case 0: break;
				  case 1: break;
				  case 2: 
					hosthandle = rbtFind(rbhosts, item); 
					hitem = gettreeitem(rbhosts, hosthandle);
					break;
				  case 3: 
					testhandle = rbtFind(rbtests, item);
					t = (testhandle == rbtEnd(rbtests)) ? NULL : gettreeitem(rbtests, testhandle);
					break;
				  case 4: newack->received = atoi(item); break;
				  case 5: newack->validuntil = atoi(item); break;
				  case 6: newack->cleartime = atoi(item); break;
				  case 7: newack->level = atoi(item); break;
				  case 8: newack->ackedby = strdup(item); break;
				  case 9: newack->msg = strdup(item); break;
				  default: break;
				}
				item = gettok(NULL, "|\n"); i++;
			}

			if (hitem && t) {
				for (log = hitem->logs; (log && (log->test != t)); log = log->next) ;
			}

			if (log && newack->msg) {
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

		if (strncmp(STRBUF(inbuf), "@@HOBBITDCHK-V1|.", 17) == 0) continue;

		item = gettok(STRBUF(inbuf), "|\n"); i = 0;
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

		dbgprintf("Status: Host=%s, test=%s\n", hostname, testname); count++;

		hosthandle = rbtFind(rbhosts, hostname);
		if (hosthandle == rbtEnd(rbhosts)) {
			/* New host */
			hitem = create_hostlist_t(hostname, hostip);
			hostcount++;
		}
		else {
			hitem = gettreeitem(rbhosts, hosthandle);
		}

		testhandle = rbtFind(rbtests, testname);
		if (testhandle == rbtEnd(rbtests)) {
			t = create_testinfo(testname);
		}
		else t = gettreeitem(rbtests, testhandle);

		originhandle = rbtFind(rborigins, originname);
		if (originhandle == rbtEnd(rborigins)) {
			origin = strdup(originname);
			rbtInsert(rborigins, origin, origin);
		}
		else origin = gettreeitem(rborigins, originhandle);

		if (hitem->logs == NULL) {
			ltail = hitem->logs = (hobbitd_log_t *) calloc(1, sizeof(hobbitd_log_t));
		}
		else {
			ltail->next = (hobbitd_log_t *)calloc(1, sizeof(hobbitd_log_t));
			ltail = ltail->next;
		}

		if (strcmp(testname, xgetenv("PINGCOLUMN")) == 0) hitem->pinglog = ltail;

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
		if (ltail->enabletime == DISABLED_UNTIL_OK) ltail->validtime = INT_MAX;
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
		if (cookie > 0) rbtInsert(rbcookies, (void *)cookie, ltail);
		ltail->cookieexpires = cookieexpires;
		ltail->metas = NULL;
		ltail->acklist = NULL;
		ltail->next = NULL;
	}

	fclose(fd);
	freestrbuffer(inbuf);
	dbgprintf("Loaded %d status logs\n", count);

	MEMDEFINE(hostip);
}


void check_purple_status(void)
{
	RbtIterator hosthandle;
	hobbitd_hostlist_t *hwalk;
	hobbitd_log_t *lwalk;
	time_t now = getcurrenttime(NULL);

	dbgprintf("-> check_purple_status\n");
	for (hosthandle = rbtBegin(rbhosts); (hosthandle != rbtEnd(rbhosts)); hosthandle = rbtNext(rbhosts, hosthandle)) {
		hwalk = gettreeitem(rbhosts, hosthandle);

		lwalk = hwalk->logs;
		while (lwalk) {
			if (lwalk->validtime < now) {
				dbgprintf("Purple log from %s %s\n", hwalk->hostname, lwalk->test->name);
				if (hwalk->hosttype == H_SUMMARY) {
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
					int newcolor = COL_PURPLE;

					/*
					 * See if this is a host where the "conn" test shows it is down.
					 * If yes, then go CLEAR, instead of PURPLE.
					 */
					if (hwalk->pinglog) {
						switch (hwalk->pinglog->color) {
						  case COL_RED:
						  case COL_YELLOW:
						  case COL_BLUE:
						  case COL_CLEAR: /* if the "route:" tag is involved */
							newcolor = COL_CLEAR;
							break;

						  default:
							newcolor = COL_PURPLE;
							break;
						}
					}

					/* Tests on dialup hosts go clear, not purple */
					if (newcolor == COL_PURPLE) {
						void *hinfo = hostinfo(hwalk->hostname);
						if (hinfo && bbh_item(hinfo, BBH_FLAG_DIALUP)) newcolor = COL_CLEAR;
					}

					handle_status(lwalk->message, "hobbitd", 
						hwalk->hostname, lwalk->test->name, lwalk->grouplist, lwalk, newcolor, NULL);
					lwalk = lwalk->next;
				}
			}
			else {
				lwalk = lwalk->next;
			}
		}
	}
	dbgprintf("<- check_purple_status\n");
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
	char *envarea = NULL;

	MEMDEFINE(colnames);

	boottime = getcurrenttime(NULL);

	/* Create our trees */
	rbhosts = rbtNew(name_compare);
	rbtests = rbtNew(name_compare);
	rborigins = rbtNew(name_compare);
	rbcookies = rbtNew(int_compare);
	rbfilecache = rbtNew(name_compare);
	rbghosts = rbtNew(name_compare);

	/* For wildcard notify's */
	create_testinfo("*");

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
		else if (argnmatch(argv[argi], "--ack-log=")) {
			char *p = strchr(argv[argi], '=');
			ackinfologfn = strdup(p+1);
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
			 clientsavemem = 0;
			 clientsavedisk = 0;
		}
		else if (argnmatch(argv[argi], "--store-clientlogs")) {
			int anypos = 0, anyneg = 0;
			char *val = strchr(argv[argi], '=');

			if (!val) {
				clientsavedisk = 1;
			}
			else {
				char *tok;

				val = strdup(val+1);
				tok = strtok(val, ",");
				while (tok) {
					testinfo_t *t;
					
					if (*tok == '!') {
						anyneg = 1;
						t = create_testinfo(tok+1);
						t->clientsave = 0;
					}
					else {
						anypos = 1;
						t = create_testinfo(tok);
						t->clientsave = 1;
					}

					tok = strtok(NULL, ",");
				}
				xfree(val);

				/* 
				 * If only positive testnames listed, let default
				 * be NOT to save, and vice versa.  If mixed, 
				 * warn about it.
				 */
				if (anypos && !anyneg) clientsavedisk = 0;
				else if (anyneg && !anypos) clientsavedisk = 1;
				else {
					errprintf("Mixed list of testnames for --store-clientlogs option, will only save listed tests.\n");
					clientsavedisk = 0;
				}

			}

			if (anypos || clientsavedisk) clientsavemem = 1;
		}
		else if (strcmp(argv[argi], "--no-download") == 0) {
			 allow_downloads = 0;
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

	errprintf("Loading hostnames\n");
	load_hostnames(bbhostsfn, NULL, get_fqdn());
	load_clientconfig();

	if (restartfn) {
		errprintf("Loading saved state\n");
		load_checkpoint(restartfn);
	}

	nextcheckpoint = getcurrenttime(NULL) + checkpointinterval;
	nextpurpleupdate = getcurrenttime(NULL) + 600;	/* Wait 10 minutes the first time */
	last_stats_time = getcurrenttime(NULL);	/* delay sending of the first status report until we're fully running */


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
	clichgchn  = setup_channel(C_CLICHG, CHAN_MASTER);
	if (clichgchn == NULL) { errprintf("Cannot setup clichg channel\n"); return 1; }
	userchn  = setup_channel(C_USER, CHAN_MASTER);
	if (userchn == NULL) { errprintf("Cannot setup user channel\n"); return 1; }

	errprintf("Setting up logfiles\n");
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	freopen("/dev/null", "r", stdin);
	if (logfn) {
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	if (ackinfologfn) {
		ackinfologfd = fopen(ackinfologfn, "a");
		if (ackinfologfd == NULL) {
			errprintf("Cannot open ack logfile %s: %s\n", ackinfologfn, strerror(errno));
		}
	}

	if (dbghost) {
		char fname[PATH_MAX];

		sprintf(fname, "%s/hobbitd.dbg", xgetenv("BBTMP"));
		dbgfd = fopen(fname, "a");
		if (dbgfd == NULL) errprintf("Cannot open debug file %s: %s\n", fname, strerror(errno));
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
		time_t now = getcurrenttime(NULL);
		int childstat;

		/* Pickup any finished child processes to avoid zombies */
		while (wait3(&childstat, WNOHANG, NULL) > 0) ;

		if (logfn && dologswitch) {
			freopen(logfn, "a", stdout);
			freopen(logfn, "a", stderr);
			if (ackinfologfd) freopen(ackinfologfn, "a", ackinfologfd);
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

				if (hwalk->hosttype == H_SUMMARY) {
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

			load_clientconfig();
		}

		if (do_purples && (now > nextpurpleupdate)) {
			nextpurpleupdate = getcurrenttime(NULL) + 60;
			check_purple_status();
		}

		if ((last_stats_time + 300) <= now) {
			char *buf;
			hobbitd_hostlist_t *h;
			testinfo_t *t;
			hobbitd_log_t *log;
			int color;

			buf = generate_stats();
			get_hts(buf, "hobbitd", "", &h, &t, NULL, &log, &color, NULL, NULL, 1, 1);
			if (!h || !t || !log) {
				errprintf("hobbitd servername MACHINE='%s' not listed in bb-hosts, dropping hobbitd status\n",
					  xgetenv("MACHINE"));
			}
			else {
				handle_status(buf, "hobbitd", h->hostname, t->name, NULL, log, color, NULL);
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
					if ((n == -1) && (errno == EAGAIN)) break; /* Do nothing */

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

					if ((n == -1) && (errno == EAGAIN)) break; /* Do nothing */

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

			now = getcurrenttime(NULL);
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
				/* Make sure our sockets are non-blocking */
				fcntl(sock, F_SETFL, O_NONBLOCK);

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
	posttochannel(clichgchn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	posttochannel(userchn, "shutdown", NULL, "hobbitd", NULL, NULL, "");
	running = 0;

	/* Close the channels */
	close_channel(statuschn, CHAN_MASTER);
	close_channel(stachgchn, CHAN_MASTER);
	close_channel(pagechn, CHAN_MASTER);
	close_channel(datachn, CHAN_MASTER);
	close_channel(noteschn, CHAN_MASTER);
	close_channel(enadischn, CHAN_MASTER);
	close_channel(clientchn, CHAN_MASTER);
	close_channel(clichgchn, CHAN_MASTER);
	close_channel(userchn, CHAN_MASTER);

	save_checkpoint();
	unlink(pidfile);

	if (dbgfd) fclose(dbgfd);

	MEMUNDEFINE(colnames);

	return 0;
}

