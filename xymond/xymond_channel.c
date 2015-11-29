/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This module receives messages from one channel of the Xymon master daemon. */
/* These messages are then forwarded to the actual worker process via stdin;  */
/* the worker process can process the messages without having to care about   */
/* the tricky details in the xymond/xymond_channel communications.            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>

#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "libxymon.h"

#include <signal.h>

/* How long should we wait when selecting the peer and seeing if it can accept data? - in microseconds */
#define PEERWAITUSEC 15

/* How often do we go through messages pending for a peer and flush stale ones */
#define PEERFLUSHSECS 5


/* How many writes max per peer if we picked up a message via semaphore */
/* We want this to be somewhat low to prevent semaphore communication from being unduly delayed */
/* which will negatively affect xymond */
static int maxpeerwrites = 1;

/* Our in-memory queue of messages received from xymond via IPC. One queue per peer. */
typedef struct xymon_msg_t {
	time_t tstamp;  /* When did the message arrive */
	char *buf;	/* The message data */
	char *bufp;	/* Next char to send */
	size_t buflen;	/* How many bytes left to send */
	struct xymon_msg_t *next;
} xymon_msg_t;


/* Our list of peers we send data to */
typedef struct xymon_peer_t {
	char *peername;

	enum { P_DOWN, P_UP, P_FAILED } peerstatus;
	xymon_msg_t *msghead, *msgtail;	/* Message queue */
	unsigned long msgcount;	/* Pending message queue size */
	time_t nextflushtime;	/* When to check for stale messages to flush out */

	enum { P_LOCAL, P_NET } peertype;
	int peersocket;				/* File descriptor receiving the data */
	time_t lastopentime;			/* Last time we attempted to connect to the peer */

	/* For P_NET peers */
	struct sockaddr_in peeraddr;		/* The IP address of the peer */

	/* For P_LOCAL peers */
	char *childcmd;				/* Command and arguments for the child process */
	char **childargs;
	pid_t childpid;				/* PID of the running worker child */
} xymon_peer_t;

void * peers;
static int localpeers = 0;
static int networkpeers = 0;
static int multipeers = 0;
static int multirun = 0;

pid_t deadpid = 0;
int childexit;

xymond_channel_t *channel = NULL;
int locatorbased = 0;
enum locator_servicetype_t locatorservice = ST_MAX;

static int running = 1;
static int gotalarm = 0;
static int dologswitch = 0;
static int hupchildren = 0;
static int pendingcount = 0;
static int messagetimeout = 30;

/* Do we run our filters before or after copying the
 * message into local memory (and letting xymond get on
 * with its semaphore work). In very heavy systems with 
 * lots of RAM, it might be more important to perform
 * semaphore changes first.
 */
static int filterlater = 0;

/*
 * chksumsize is the space left in front of the message buffer, to
 * allow room for a message digest checksum to be added to the
 * message. Since we use an MD5 hash, this will be 32 bytes 
 * plus a one-char marker.
 */
static int checksumsize = 0;

/* How long to wait when first launching an
 * external command before attaching to the 
 * shared memory.
 */
static int initialdelay = 0;

void addnetpeer(char *peername)
{
	xymon_peer_t *newpeer;
	struct in_addr addr;
	char *oneip;
	int peerport = 0;
	char *delim;

	dbgprintf("Adding network peer %s\n", peername);

	oneip = strdup(peername);

	delim = strchr(oneip, ':');
	if (delim) {
		*delim = '\0';
		peerport = atoi(delim+1);
	}

	if (inet_aton(oneip, &addr) == 0) {
		/* peer is not an IP - do DNS lookup */

		struct hostent *hent;

		hent = gethostbyname(oneip);
		if (hent) {
			char *realip;

			memcpy(&addr, *(hent->h_addr_list), sizeof(struct in_addr));
			realip = inet_ntoa(addr);
			if (inet_aton(realip, &addr) == 0) {
				errprintf("Invalid IP address for %s (%s)\n", oneip, realip);
				goto done;
			}
		}
		else {
			errprintf("Cannot determine IP address of peer %s\n", oneip);
			goto done;
		}
	}

	if (peerport == 0) peerport = atoi(xgetenv("XYMONDPORT"));

	newpeer = calloc(1, sizeof(xymon_peer_t));
	newpeer->peername = (char *)malloc(strlen(peername)+12);
	snprintf(newpeer->peername, (strlen(peername)+10), "%s:%d", peername, ++networkpeers);
	newpeer->peerstatus = P_DOWN;
	newpeer->peertype = P_NET;
	newpeer->peeraddr.sin_family = AF_INET;
	newpeer->peeraddr.sin_addr.s_addr = addr.s_addr;
	newpeer->peeraddr.sin_port = htons(peerport);

	xtreeAdd(peers, newpeer->peername, newpeer);

done:
	xfree(oneip);
}


void addlocalpeer(char *childcmd, char **childargs)
{
	xymon_peer_t *newpeer;
	int i, count;

	dbgprintf("Adding local peer using command %s\n", childcmd);

	for (count=0; (childargs[count]); count++) ;

	newpeer = (xymon_peer_t *)calloc(1, sizeof(xymon_peer_t));
	newpeer->peername = (char *)malloc(strlen(childcmd)+12);
	snprintf(newpeer->peername, (strlen(childcmd)+10), "%s:%d", childcmd, ++localpeers);
	newpeer->peerstatus = P_DOWN;
	newpeer->peertype = P_LOCAL;
	newpeer->childcmd = strdup(childcmd);
	newpeer->childargs = (char **)calloc(count+1, sizeof(char *));
	for (i=0; (i<count); i++) newpeer->childargs[i] = strdup(childargs[i]);

	xtreeAdd(peers, newpeer->peername, newpeer);
}


void openconnection(xymon_peer_t *peer)
{
	int n;
	int pfd[2];
	pid_t childpid;
	time_t now;

	peer->peerstatus = P_DOWN;

	now = gettimer();
	if (now < (peer->lastopentime + 60)) return;	/* Will only attempt one open per minute */

	dbgprintf("Connecting to peer %s:%d\n", inet_ntoa(peer->peeraddr.sin_addr), ntohs(peer->peeraddr.sin_port));

	peer->lastopentime = now;
	switch (peer->peertype) {
	  case P_NET:
		/* Get a socket, and connect to the peer */
		peer->peersocket = socket(PF_INET, SOCK_STREAM, 0);
		if (peer->peersocket == -1) {
			errprintf("Cannot get socket: %s\n", strerror(errno));
			return;
		}

		n = connect(peer->peersocket, (struct sockaddr *)&peer->peeraddr, sizeof(peer->peeraddr));
		if (n == -1) {
			errprintf("Cannot connect to peer %s:%d : %s\n", 
				inet_ntoa(peer->peeraddr.sin_addr), ntohs(peer->peeraddr.sin_port), 
				strerror(errno));
			return;
		}
		break;

	  case P_LOCAL:
		/* Create a pipe to the child handler program, and run it */
		n = pipe(pfd);
		if (n == -1) {
			errprintf("Could not get a pipe: %s\n", strerror(errno));
			return;
		}

		childpid = fork();
		if (childpid == -1) {
			errprintf("Could not fork channel handler: %s\n", strerror(errno));
			return;
		}
		else if (childpid == 0) {
			/* The channel handler child */
			if (logfn) {
				char *logfnenv = (char *)malloc(strlen(logfn) + 30);
				sprintf(logfnenv, "XYMONCHANNEL_LOGFILENAME=%s", logfn);
				putenv(logfnenv);
			}

			dbgprintf("Child '%s' started (PID %d), about to fork\n", peer->childcmd, (int)getpid());

			n = dup2(pfd[0], STDIN_FILENO);
			close(pfd[0]); close(pfd[1]);
			n = execvp(peer->childcmd, peer->childargs);
			
			/* We should never go here */
			errprintf("exec() failed for child command %s: %s\n", 
				peer->childcmd, strerror(errno));
			exit(1);
		}

		/* Parent process continues */
		close(pfd[0]);
		peer->peersocket = pfd[1];
		peer->childpid = childpid;
		break;
	}

	fcntl(peer->peersocket, F_SETFL, O_NONBLOCK);
	peer->peerstatus = P_UP;
	dbgprintf("Peer is UP\n");
}



void flushmessage(xymon_peer_t *peer)
{
	xymon_msg_t *zombie;

	zombie = peer->msghead;

	peer->msghead = peer->msghead->next;
	if (peer->msghead == NULL) peer->msgtail = NULL;

	xfree(zombie->buf);
	xfree(zombie);
	peer->msgcount--;
	pendingcount--;
}

static void addmessage_onepeer(xymon_peer_t *peer, char *inbuf, size_t inlen)
{
	xymon_msg_t *newmsg;

	/* 
	 * If we've flagged the peer as FAILED, then change status to DOWN so
	 * we will attempt to reconnect to the peer. The locator believes it is
	 * up and running, so it probably is ...
	 */
	if (peer->peerstatus == P_FAILED) peer->peerstatus = P_DOWN;

	/* If the peer is not up, we will only permit ONE message in the queue. */
	if (peer->peerstatus != P_UP) {
		errprintf("Peer not up, flushing message queue\n");
		while (peer->msghead) flushmessage(peer);
		if (peer->msgcount) { errprintf("xymond_channel: flushed all messages, but msgcount is %lu\n", peer->msgcount); peer->msgcount = 0; }
	}

	newmsg = (xymon_msg_t *) calloc(1, sizeof(xymon_msg_t));
	newmsg->tstamp = gettimer();
	newmsg->buf = (char *)malloc(inlen + 1);
	memcpy(newmsg->buf, inbuf, inlen);
	newmsg->bufp = newmsg->buf;
	newmsg->buflen = inlen;

	if (peer->msghead == NULL) {
		peer->msghead = peer->msgtail = newmsg;
	}
	else {
		peer->msgtail->next = newmsg;
		peer->msgtail = newmsg;
	}

	peer->msgcount++;
	pendingcount++;
}

int addmessage(char *inbuf, size_t inlen)
{
	xtreePos_t phandle;
	xymon_peer_t *peer;
	int bcastmsg = 0;

	if (locatorbased) {
		char *hostname, *hostend, *peerlocation;

		/* xymond sends us messages with the KEY in the first field, between a '/' and a '|' */
		hostname = inbuf + strcspn(inbuf, "/|\r\n");
		if (*hostname != '/') {
			errprintf("No key field in message, dropping it\n");
			return -1; /* Malformed input */
		}
		hostname++;
		bcastmsg = (*hostname == '*');
		if (!bcastmsg) {
			/* Lookup which server handles this host */
			hostend = hostname + strcspn(hostname, "|\r\n");
			if (*hostend != '|') {
				errprintf("No delimiter found in input, dropping it\n");
				return -1; /* Malformed input */
			}
			*hostend = '\0';
			peerlocation = locator_query(hostname, locatorservice, NULL);

			/*
			 * If we get no response, or an empty response, 
			 * then there is no server capable of handling this
			 * request.
			 */
			if (!peerlocation || (*peerlocation == '\0')) {
				errprintf("No response from locator for %s/%s, dropping it\n",
					  servicetype_names[locatorservice], hostname);
				return -1;
			}

			*hostend = '|';
			phandle = xtreeFind(peers, peerlocation);
			if (phandle == xtreeEnd(peers)) {
				/* New peer - register it */
				addnetpeer(peerlocation);
				phandle = xtreeFind(peers, peerlocation);
			}
		}
	}

	if ((multipeers && !multirun) || bcastmsg) {
		for (phandle = xtreeFirst(peers); (phandle != xtreeEnd(peers)); phandle = xtreeNext(peers, phandle)) {
			peer = (xymon_peer_t *)xtreeData(peers, phandle);

			addmessage_onepeer(peer, inbuf, inlen);
		}
	}
	else if (multipeers && multirun) {
		/* Find the peer with the smallest number of messages in queue -- this could be expanded 
		 * easily to a more general purpose load balancing mechanism in the future.
		 */
		unsigned long minqueuesize = 999999999; /* if you have more than this many pending, you have other problems */
		xymon_peer_t *bestpeer = NULL;

		for (phandle = xtreeFirst(peers); (phandle != xtreeEnd(peers)); phandle = xtreeNext(peers, phandle)) {
			peer = (xymon_peer_t *)xtreeData(peers, phandle);
			if (peer->msgcount < minqueuesize) { bestpeer = peer; minqueuesize = peer->msgcount; }
		}
		if (bestpeer) addmessage_onepeer(bestpeer, inbuf, inlen);
		else errprintf("xymond_channel: BUG: multirun could not find any peers? Message dropped.\n");

	}
	else {
		phandle = xtreeFirst(peers);
		if (phandle == xtreeEnd(peers)) {
			errprintf("No peer found to handle message, dropping it\n");
			return -1;
		}
		peer = (xymon_peer_t *)xtreeData(peers, phandle);

		addmessage_onepeer(peer, inbuf, inlen);
	}

	return 0;
}

void shutdownconnection(xymon_peer_t *peer)
{
	if (peer->peerstatus != P_UP) return;

	peer->peerstatus = P_DOWN;

	switch (peer->peertype) {
	  case P_LOCAL:
		close(peer->peersocket);
		peer->peersocket = -1;
		if (peer->childpid > 0) kill(peer->childpid, SIGTERM);
		peer->childpid = 0;
		break;

	  case P_NET:
		shutdown(peer->peersocket, SHUT_RDWR);
		close(peer->peersocket);
		peer->peersocket = -1;
		break;
	}

	/* Any messages queued are discarded */
	while (peer->msghead) flushmessage(peer);
	if (peer->msgcount) { errprintf("xymond_channel: flushed all messages, but msgcount is %lu\n", peer->msgcount); peer->msgcount = 0; }
	peer->msghead = peer->msgtail = NULL;
}


void sig_handler(int signum)
{
	switch (signum) {
	  case SIGTERM:
	  case SIGINT:
		/* Shutting down. */
		running = 0;
		break;

	  case SIGHUP:
		/* Rotate our log file */
		dologswitch = 1;
		hupchildren = 1;
		break;

	  case SIGCHLD:
		/* Our worker child died. Avoid zombies. */
		deadpid = wait(&childexit);
		break;

	  case SIGALRM:
		gotalarm = 1;
		break;
	}
}


int main(int argc, char *argv[])
{
	int multilocal = 0;
	int daemonize = 0;
	int cnid = -1;
	char *inbuf = NULL;
	size_t msgsz = 0;
	pcre *msgfilter = NULL;
	pcre *stdfilter = NULL;

	int argi;
	struct sigaction sa;
	xtreePos_t handle;


	libxymon_init(argv[0]);

	/* Don't save the error buffer */
	save_errbuf = 0;

	/* Create the peer container */
	peers = xtreeNew(strcasecmp);

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--channel=")) {
			char *cn = strchr(argv[argi], '=') + 1;

			for (cnid = C_STATUS; (channelnames[cnid] && strcmp(channelnames[cnid], cn)); cnid++) ;
			if (channelnames[cnid] == NULL) cnid = -1;
		}
		else if (argnmatch(argv[argi], "--msgtimeout")) {
			char *p = strchr(argv[argi], '=');
			messagetimeout = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--initialdelay") || argnmatch(argv[argi], "--delay")) {
			char *p = strchr(argv[argi], '=');
			initialdelay = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--daemon")) {
			daemonize = 1;
		}
		else if (argnmatch(argv[argi], "--no-daemon")) {
			daemonize = 0;
		}
		else if (argnmatch(argv[argi], "--locator=")) {
			char *p = strchr(argv[argi], '=');
			locator_init(p+1);
			locatorbased = 1;
		}
		else if (argnmatch(argv[argi], "--service=")) {
			char *p = strchr(argv[argi], '=');
			locatorservice = get_servicetype(p+1);
		}
		else if (argnmatch(argv[argi], "--filter=")) {
			char *p = strchr(argv[argi], '=');
			msgfilter = compileregex(p+1);
			if (!msgfilter) {
				errprintf("Invalid filter (bad expression): %s\n", p+1);
			}
			else {
				stdfilter = firstlineregex("^@@(logrotate|shutdown|drophost|droptest|renamehost|renametest)");
			}
		}
		else if (argnmatch(argv[argi], "--filterlater")) {
			filterlater = 1;
		}
		else if (argnmatch(argv[argi], "--md5")) {
			checksumsize = 33;
		}
		else if (argnmatch(argv[argi], "--no-md5")) {
			checksumsize = 0;
		}
		else if (argnmatch(argv[argi], "--multilocal")) {
			multilocal = 1;
			dbgprintf("xymond_channel: sending to multiple local peers\n");
		}
		else if (argnmatch(argv[argi], "--multirun")) {
			char *p = strchr(argv[argi], '=');
			if (p) {
				multirun = atoi(p+1);
				logprintf("xymond_channel: sending messages to one of %d worker copies\n", multirun);
			} else {
				multirun = 1;
				if (!multilocal) errprintf("xymond_channel: bare '--multirun' seen, assuming --multilocal\n");
				multilocal = 1;
			}
		}
		else if (argnmatch(argv[argi], "--maxpeerwrites")) {
			char *p = strchr(argv[argi], '=');
			maxpeerwrites = atoi(p+1);
			dbgprintf("xymond_channel: max writes attempted when msg on channel: %d\n", maxpeerwrites);
			if (maxpeerwrites <= 0) maxpeerwrites = 1;	/* it's a do ... while later on, basically */
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
		else if (multilocal) {
			char *childcmd;
			char **childargs;
			childargs = (char **) calloc(2, sizeof(char *));

			if (multirun && (multirun > 1)) errprintf("ERROR: specified --multirun=%d; '%d' ignored in multilocal mode\n", multirun, multirun);

			while (argi < argc) {
				childcmd = argv[argi];
				childargs[0] = argv[argi];
				addlocalpeer(childcmd, childargs);
				argi++;
			}
			xfree(childargs);
		}
		else {
			char *childcmd;
			char **childargs;
			int i = 0;

			childcmd = argv[argi];
			childargs = (char **) calloc((1 + argc - argi), sizeof(char *));
			while (argi < argc) { childargs[i++] = argv[argi++]; }
			addlocalpeer(childcmd, childargs);

			/* if --multirun=N given, repeat as many _more_ times as needed */
			/* --multirun=1 and no --multilocal is a no-op */
			for ( i = 1; i < multirun; i++ ) addlocalpeer(childcmd, childargs);

			xfree(childargs);
		}
	}

	/* Sanity checks */
	if (cnid == -1) {
		errprintf("No channel/unknown channel specified\n");
		return 1;
	}
	if (locatorbased && (locatorservice == ST_MAX)) {
		errprintf("Must specify --service when using locator\n");
		return 1;
	}
	if (!locatorbased && (xtreeFirst(peers) == xtreeEnd(peers))) {
		errprintf("Must specify command for local worker\n");
		return 1;
	}
	if (!logfn && getenv("XYMONLAUNCH_LOGFILENAME")) {
		/* No log file on the command line, but our STDOUT is already */
		/* being piped somewhere. Record this for when it's time to re-open on rotation */
		logfn = xgetenv("XYMONLAUNCH_LOGFILENAME");
		dbgprintf("xymond_channel: Already logging out to %s, per xymonlaunch\n", logfn);
	}


	/* Do cache responses to avoid doing too many lookups */
	if (locatorbased) locator_prepcache(locatorservice, 0);

	/* Go daemon */
	if (daemonize) {
		/* Become a daemon */
		pid_t daemonpid = fork();
		if (daemonpid < 0) {
			/* Fork failed */
			errprintf("Could not fork child\n");
			exit(1);
		}
		else if (daemonpid > 0) {
			/* Parent creates PID file and exits */
			FILE *fd = NULL;
			if (pidfn) fd = fopen(pidfn, "w");
			if (fd) {
				fprintf(fd, "%d\n", (int)daemonpid);
				fclose(fd);
			}
			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	/* Catch signals */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
	signal(SIGALRM, SIG_IGN);

	/* Switch stdout/stderr to the logfile, if one was specified */
	reopen_file("/dev/null", "r", stdin);	/* xymond_channel's stdin is not used */
	if (logfn) {
		reopen_file(logfn, "a", stdout);
		reopen_file(logfn, "a", stderr);
	}

	/* Connect to all of our peers (ie, launch the process(es)) before we even
	 * attach to the channel */
	if (!locatorbased) {
		for (handle = xtreeFirst(peers); (handle != xtreeEnd(peers)); handle = xtreeNext(peers, handle)) {
			xymon_peer_t *pwalk;
			pwalk = (xymon_peer_t *) xtreeData(peers, handle);
			openconnection(pwalk);
		}
		if (initialdelay > 0) {
			logprintf("xymond_channel: Sleeping for %d seconds before attaching to channel\n", initialdelay);
			sleep(initialdelay);
		}
	}

	/* Record if we're dealing with multiple local peers */
	multipeers = ((networkpeers + localpeers) > 1);

	/* Attach to the channel */
	channel = setup_channel(cnid, CHAN_CLIENT);
	if (channel != NULL) {
		inbuf = (char *)malloc(1024*shbufsz(cnid) + checksumsize + 1);
		if (inbuf == NULL) {
			errprintf("xymond_channel: Could not allocate sufficient memory for %s channel buffer size\n", channelnames[cnid]);
			running = 0;
		}
	}
	else {
		errprintf("Channel not available\n");
		running = 0;
	}

	while (running || pendingcount) {
		/* 
		 * Wait for GOCLIENT to go up.
		 *
		 * Note that we use IPC_NOWAIT if there are messages in the
		 * queue, because then we just want to pick up a message if
		 * there is one, and if not we want to continue pushing the
		 * queued data to the worker.
		 */
		struct sembuf s;
		int n, gotmsg;
		time_t msgtimeout, currenttime;

		if (deadpid != 0) {
			char *cause = "Unknown";
			int ecode = -1;

			if (WIFEXITED(childexit)) { cause = "Exit status"; ecode = WEXITSTATUS(childexit); }
			else if (WIFSIGNALED(childexit)) { cause = "Signal"; ecode = WTERMSIG(childexit); }
			errprintf("Child process %d died: %s %d\n", deadpid, cause, ecode);
			deadpid = 0;
		}

		if (hupchildren) {
			/* Propagate HUP to children, but only if they're already up */
			for (handle = xtreeFirst(peers); (handle != xtreeEnd(peers)); handle = xtreeNext(peers, handle)) {
				xymon_peer_t *pwalk;
				pwalk = (xymon_peer_t *) xtreeData(peers, handle);
				if (pwalk->peerstatus == P_UP && pwalk->peertype == P_LOCAL && pwalk->childpid > 0) kill(pwalk->childpid, SIGHUP);
			}
			hupchildren = 0;
		}


	    /* Only do our semaphore work if we're still connected to a channel */
	    if (running) {

		s.sem_num = GOCLIENT; s.sem_op  = -1; s.sem_flg = ((pendingcount > 0) ? IPC_NOWAIT : 0);
		n = semop(channel->semid, &s, 1);

		/* This is where we'll first find some fatal errors */
		if ((n == -1) && (errno != EAGAIN) && (errno != EINTR) ) {
			dbgprintf("xymond_channel: Semaphore wait failed; can't continue: %s\n", strerror(errno));
			running = 0;
		} 

		if (n == 0) {
			/*
			 * GOCLIENT went high, and so we got alerted about a new
			 * message arriving. Copy the message to our own buffer queue.
			 */

			if (filterlater || !msgfilter || matchregex(channel->channelbuf, msgfilter) || matchregex(channel->channelbuf, stdfilter)) {
				msgsz = strlen(channel->channelbuf);
				memcpy(inbuf+checksumsize, channel->channelbuf, msgsz+1); /* Include \0 */
			}
			else {
				msgsz = 0; *inbuf = '\0';
			}

			/* 
			 * Now we have safely stored the new message in our buffer.
			 * Wait until any other clients on the same channel have picked up 
			 * this message (GOCLIENT reaches 0).
			 *
			 * We wrap this into an alarm handler, because it can occasionally
			 * fail, causing the whole system to lock up. We don't want that....
			 * We'll set the alarm to trigger after 1 second. Experience shows
			 * that we'll either succeed in a few milliseconds, or fail completely
			 * and wait the full alarm-timer duration.
			 */
			gotalarm = 0; signal(SIGALRM, sig_handler); alarm(2); 
			do {
				s.sem_num = GOCLIENT; s.sem_op  = 0; s.sem_flg = 0;
				n = semop(channel->semid, &s, 1);
			} while ((n == -1) && (errno == EAGAIN) && running && (!gotalarm));
			signal(SIGALRM, SIG_IGN);

			if (gotalarm) {
				errprintf("Gave up waiting for GOCLIENT to go low.\n");
			}

			/* 
			 * Let master know we got it by downing BOARDBUSY.
			 * This should not block, since BOARDBUSY is upped
			 * by the master just before he ups GOCLIENT.
			 */
			do {
				s.sem_num = BOARDBUSY; s.sem_op  = -1; s.sem_flg = IPC_NOWAIT;
				n = semop(channel->semid, &s, 1);
			} while ((n == -1) && (errno == EINTR));
			if (n == -1) {
				errprintf("Tried to down BOARDBUSY: %s\n", strerror(errno));
			}

			/* If we postponed filtering after we handled the semaphore logic, do it now */
			if (filterlater && msgfilter && !matchregex(inbuf, msgfilter) && !matchregex(inbuf, stdfilter)) { msgsz = 0; *inbuf = '\0'; }

			if (msgsz) {
				/*
				 * See if they want us to rotate logs. We pass this on to
				 * the worker module as well, but must handle our own logfile.
				 */
				if (strncmp(inbuf+checksumsize, "@@logrotate", 11) == 0) {
					dologswitch = 1;
				}

				/*
				 * Are we shutting down via channel command? Flag for closing.
				 */
				if (strncmp(inbuf+checksumsize, "@@shutdown", 10) == 0) {
					logprintf("xymond_channel: received shutdown message\n");
					running = 0;
				}

				if (checksumsize > 0) {
					char *sep1 = inbuf + checksumsize + strcspn(inbuf+checksumsize, "#|\n");

					if (*sep1 == '#') {
						/* 
						 * Add md5 hash of the message. I.e. transform the header line from
						 *   "@@%s#%u/%s|%d.%06d| channelmarker, seq, hostname, tstamp.tv_sec, tstamp.tv_usec
						 * to
						 *   "@@%s:%s#%u/%s|%d.%06d| channelmarker, hashstr, seq, hostname, tstamp.tv_sec, tstamp.tv_usec
						 */
						char *hashstr = md5hash(inbuf+checksumsize);
						int hlen = sep1 - (inbuf + checksumsize);

						memmove(inbuf, inbuf+checksumsize, hlen);
						*(inbuf + hlen) = ':';
						memcpy(inbuf+hlen+1, hashstr, strlen(hashstr));
					}
					else {
						/* No sequence number (control message). Skip checksum for these */
						memmove(inbuf, inbuf+checksumsize, msgsz+1);
					}
				}


				/*
				 * Put the new message on our outbound queue.
				 */
				if (addmessage(inbuf, msgsz) != 0) {
					/* Failed to queue message */
					errprintf("xymond_channel: Failed to queue message, moving on\n");
				}
				gotmsg = 1;			/* since we received something from xymond, don't dally too long */
			}
		}
		else {
			if (errno != EAGAIN) {
				dbgprintf("Semaphore wait aborted: %s\n", strerror(errno));
				continue;
			}
		}

	    }
	    else if (channel) {

			pid_t daemonpid;

			/* Not running any more but may still have pending messages,
			 * Shut down our channel but keep looping until the peers 
			 * have received all pending messages
			 *
			 * Since we registered with SEM_UNDO, fork again and let our
			 * parent get killed to do the detach safely.
			 */

			logprintf("xymond_channel: detaching from channel\n");

			daemonpid = fork();
			if (daemonpid < 0) {
				/* Fork failed */
				errprintf("xymond_channel: Could not fork ourself for channel cleanup; aborting\n");
				exit(1);
			}
			else if (daemonpid > 0) {
				/* Parent exits immediately. This should remove our semaphore
				 * while keeping our child attached to the peers and sending off
				 * messages that may be buffered.
				 */
				exit(0);
			}
			/* Child (daemon) continues here */
			setsid();

			/* Just detach. Our parent SEM_UNDOs us when it exits above */
			close_channel(channel, CHAN_CLIENT);
			/* Regardless of result, NULL the channel pointer since that's what we check for */
			channel = NULL;
			if (inbuf) xfree(inbuf);
	    }
	    /* end if (running || pendingcount) else if (channel) */

		/* 
		 * We've picked up messages from the master. Now we 
		 * must push them to the worker process. Since there 
		 * is no way to hang off both a semaphore and select(),
		 * we'll just push as much data as possible into the 
		 * pipe. If we get to a point where we would block,
		 * then wait a teeny bit of time and restart the 
		 * whole loop with checking for new messages from the
		 * master etc.
		 *
		 * In theory, this could become an almost busy-wait loop.
		 * In practice, however, the queue will be empty most
		 * of the time because we'll just shove the data to the
		 * worker child.
		 */
		currenttime = msgtimeout = gettimer();
		msgtimeout -= messagetimeout;

		for (handle = xtreeFirst(peers); (handle != xtreeEnd(peers)); handle = xtreeNext(peers, handle)) {
			int canwrite = 1, hasfailed = 0;
			xymon_peer_t *pwalk;
			int flushcount = 0;
			int writecount = maxpeerwrites;

			pwalk = (xymon_peer_t *) xtreeData(peers, handle);
			if (pwalk->msghead == NULL) continue; /* Ignore peers with nothing queued */

			switch (pwalk->peerstatus) {
			  case P_UP:
				canwrite = 1;
				break;

			  case P_DOWN:
				if (running) openconnection(pwalk);
				canwrite = (pwalk->peerstatus == P_UP);
				break;

			  case P_FAILED:
				canwrite = 0;
				break;
			}


			/* Occasionally see if we have stale messages queued */
			if (currenttime > pwalk->nextflushtime) {
				while (pwalk->msghead && (pwalk->msghead->tstamp < msgtimeout)) {
					flushmessage(pwalk);
					flushcount++;
				}
				pwalk->nextflushtime = currenttime + PEERFLUSHSECS;
			}

			/* If we have write errors and are already closing up, just flush everything */
			if (!running && !canwrite) {
				while (pwalk->msghead) { flushmessage(pwalk); flushcount++; }
			}

			if (flushcount) {
				errprintf("Flushed %d stale messages for %s:%d\n",
					  flushcount,
				  	  inet_ntoa(pwalk->peeraddr.sin_addr), 
					  ntohs(pwalk->peeraddr.sin_port));
			}

			while (pwalk->msghead && canwrite) {
				fd_set fdwrite;
				struct timeval tmo;

				/* Check that this peer is ready for writing. */
				FD_ZERO(&fdwrite); FD_SET(pwalk->peersocket, &fdwrite);
				tmo.tv_sec = 0; tmo.tv_usec = PEERWAITUSEC;
				n = select(pwalk->peersocket+1, NULL, &fdwrite, NULL, &tmo);
				if (n == -1) {
					errprintf("select() failed: %s\n", strerror(errno));
					canwrite = 0; 
					hasfailed = 1;
					continue;
				}
				else if ((n == 0) || (!FD_ISSET(pwalk->peersocket, &fdwrite))) {
					canwrite = 0;
					continue;
				}

				n = write(pwalk->peersocket, pwalk->msghead->bufp, pwalk->msghead->buflen);
				if (n >= 0) {
					pwalk->msghead->bufp += n;
					pwalk->msghead->buflen -= n;
					if (pwalk->msghead->buflen == 0) flushmessage(pwalk);
					/* stop on this peer after we've hit our max */
					if (gotmsg && !--writecount) canwrite = 0;
				}
				else if (errno == EAGAIN) {
					/*
					 * Write would block ... stop for now. 
					 */
					canwrite = 0;
				}
				else {
					hasfailed = 1;
				}

				if (hasfailed) {
					/* Write failed, or message grew stale */
					errprintf("Peer at %s:%d failed: %s\n",
						  inet_ntoa(pwalk->peeraddr.sin_addr), ntohs(pwalk->peeraddr.sin_port),
						  strerror(errno));
					canwrite = 0;
					shutdownconnection(pwalk);
					if (pwalk->peertype == P_NET) locator_serverdown(pwalk->peername, locatorservice);
					pwalk->peerstatus = P_FAILED;
				}
			}
		}
		if (dologswitch) {
			logprintf("xymond_channel: reopening logfiles\n");
			if (logfn) {
				reopen_file(logfn, "a", stdout);
				reopen_file(logfn, "a", stderr);
				logprintf("xymond_channel: reopened logfiles\n");
			}
			dologswitch = 0;
		}
	}

	/* Detach from channels if we haven't already */
	if (channel) close_channel(channel, CHAN_CLIENT);

	/* Free the buffer to be pedantic */
	if (inbuf) xfree(inbuf);

	/* Close peer connections */
	for (handle = xtreeFirst(peers); (handle != xtreeEnd(peers)); handle = xtreeNext(peers, handle)) {
		xymon_peer_t *pwalk = (xymon_peer_t *) xtreeData(peers, handle);
		shutdownconnection(pwalk);
	}

	/* Remove the PID file */
	if (pidfn) unlink(pidfn);

	return 0;
}

