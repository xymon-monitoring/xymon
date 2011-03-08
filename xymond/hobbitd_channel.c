/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This module receives messages from one channel of the Hobbit master daemon.*/
/* These messages are then forwarded to the actual worker process via stdin;  */
/* the worker process can process the messages without having to care about   */
/* the tricky details in the hobbitd/hobbitd_channel communications.          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
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

#include "libbbgen.h"

#include "hobbitd_ipc.h"

#include <signal.h>


#define MSGTIMEOUT 30	/* Seconds */


/* Our in-memory queue of messages received from hobbitd via IPC. One queue per peer. */
typedef struct hobbit_msg_t {
	time_t tstamp;  /* When did the message arrive */
	char *buf;	/* The message data */
	char *bufp;	/* Next char to send */
	int buflen;	/* How many bytes left to send */
	struct hobbit_msg_t *next;
} hobbit_msg_t;


/* Our list of peers we send data to */
typedef struct hobbit_peer_t {
	char *peername;

	enum { P_DOWN, P_UP, P_FAILED } peerstatus;
	hobbit_msg_t *msghead, *msgtail;	/* Message queue */

	enum { P_LOCAL, P_NET } peertype;
	int peersocket;				/* File descriptor receiving the data */
	time_t lastopentime;			/* Last time we attempted to connect to the peer */

	/* For P_NET peers */
	struct sockaddr_in peeraddr;		/* The IP address of the peer */

	/* For P_LOCAL peers */
	char *childcmd;				/* Command and arguments for the child process */
	char **childargs;
	pid_t childpid;				/* PID of the running worker child */
} hobbit_peer_t;

RbtHandle peers;

hobbitd_channel_t *channel = NULL;
char *logfn = NULL;
int locatorbased = 0;
enum locator_servicetype_t locatorservice = ST_MAX;

static int running = 1;
static int gotalarm = 0;
static int pendingcount = 0;


void addnetpeer(char *peername)
{
	hobbit_peer_t *newpeer;
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

	if (peerport == 0) peerport = atoi(xgetenv("BBPORT"));

	newpeer = calloc(1, sizeof(hobbit_peer_t));
	newpeer->peername = strdup(peername);
	newpeer->peerstatus = P_DOWN;
	newpeer->peertype = P_NET;
	newpeer->peeraddr.sin_family = AF_INET;
	newpeer->peeraddr.sin_addr.s_addr = addr.s_addr;
	newpeer->peeraddr.sin_port = htons(peerport);

	rbtInsert(peers, newpeer->peername, newpeer);

done:
	xfree(oneip);
}


void addlocalpeer(char *childcmd, char **childargs)
{
	hobbit_peer_t *newpeer;
	int i, count;

	dbgprintf("Adding local peer using command %s\n", childcmd);

	for (count=0; (childargs[count]); count++) ;

	newpeer = (hobbit_peer_t *)calloc(1, sizeof(hobbit_peer_t));
	newpeer->peername = strdup("");
	newpeer->peerstatus = P_DOWN;
	newpeer->peertype = P_LOCAL;
	newpeer->childcmd = strdup(childcmd);
	newpeer->childargs = (char **)calloc(count+1, sizeof(char *));
	for (i=0; (i<count); i++) newpeer->childargs[i] = strdup(childargs[i]);

	rbtInsert(peers, newpeer->peername, newpeer);
}


void openconnection(hobbit_peer_t *peer)
{
	int n;
	int pfd[2];
	pid_t childpid;
	time_t now;

	peer->peerstatus = P_DOWN;

	now = getcurrenttime(NULL);
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
				sprintf(logfnenv, "HOBBITCHANNEL_LOGFILENAME=%s", logfn);
				putenv(logfnenv);
			}

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



void flushmessage(hobbit_peer_t *peer)
{
	hobbit_msg_t *zombie;

	zombie = peer->msghead;

	peer->msghead = peer->msghead->next;
	if (peer->msghead == NULL) peer->msgtail = NULL;

	xfree(zombie->buf);
	xfree(zombie);
	pendingcount--;
}

static void addmessage_onepeer(hobbit_peer_t *peer, char *inbuf, int inlen)
{
	hobbit_msg_t *newmsg;

	newmsg = (hobbit_msg_t *) calloc(1, sizeof(hobbit_msg_t));
	newmsg->tstamp = getcurrenttime(NULL);
	newmsg->buf = newmsg->bufp = inbuf;
	newmsg->buflen = inlen;

	/* 
	 * If we've flagged the peer as FAILED, then change status to DOWN so
	 * we will attempt to reconnect to the peer. The locator believes it is
	 * up and running, so it probably is ...
	 */
	if (peer->peerstatus == P_FAILED) peer->peerstatus = P_DOWN;

	/* If the peer is down, we will only permit ONE message in the queue. */
	if (peer->peerstatus != P_UP) {
		errprintf("Peer not up, flushing message queue\n");
		while (peer->msghead) flushmessage(peer);
	}

	if (peer->msghead == NULL) {
		peer->msghead = peer->msgtail = newmsg;
	}
	else {
		peer->msgtail->next = newmsg;
		peer->msgtail = newmsg;
	}

	pendingcount++;
}

int addmessage(char *inbuf)
{
	RbtIterator phandle;
	hobbit_peer_t *peer;
	int bcastmsg = 0;
	int inlen = strlen(inbuf);

	if (locatorbased) {
		char *hostname, *hostend, *peerlocation;

		/* hobbitd sends us messages with the KEY in the first field, between a '/' and a '|' */
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
			*hostend = '|';

			/*
			 * If we get no response, or an empty response, 
			 * then there is no server capable of handling this
			 * request.
			 */
			if (!peerlocation || (*peerlocation == '\0')) {
				errprintf("No response from locator for %s/%s, dropping it\n",
					  locatorservice, hostname);
				return -1;
			}

			phandle = rbtFind(peers, peerlocation);
			if (phandle == rbtEnd(peers)) {
				/* New peer - register it */
				addnetpeer(peerlocation);
				phandle = rbtFind(peers, peerlocation);
			}
		}
	}
	else {
		phandle = rbtFind(peers, "");
	}

	if (bcastmsg) {
		for (phandle = rbtBegin(peers); (phandle != rbtEnd(peers)); phandle = rbtNext(peers, phandle)) {
			peer = (hobbit_peer_t *)gettreeitem(peers, phandle);

			addmessage_onepeer(peer, inbuf, inlen);
		}
	}
	else {
		if (phandle == rbtEnd(peers)) {
			errprintf("No peer found to handle message, dropping it\n");
			return -1;
		}
		peer = (hobbit_peer_t *)gettreeitem(peers, phandle);

		addmessage_onepeer(peer, inbuf, inlen);
	}

	return 0;
}

void shutdownconnection(hobbit_peer_t *peer)
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
	peer->msghead = peer->msgtail = NULL;
}


void sig_handler(int signum)
{
	int childexit;

	switch (signum) {
	  case SIGTERM:
	  case SIGINT:
		/* Shutting down. */
		running = 0;
		break;

	  case SIGCHLD:
		/* Our worker child died. Avoid zombies. */
		wait(&childexit);
		break;

	  case SIGALRM:
		gotalarm = 1;
		break;
	}
}


int main(int argc, char *argv[])
{
	int daemonize = 0;
	char *pidfile = NULL;
	char *envarea = NULL;
	int cnid = -1;

	int argi;
	struct sigaction sa;
	RbtIterator handle;


	/* Dont save the error buffer */
	save_errbuf = 0;

	/* Create the peer container */
	peers = rbtNew(name_compare);

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--debug")) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--channel=")) {
			char *cn = strchr(argv[argi], '=') + 1;

			for (cnid = C_STATUS; (channelnames[cnid] && strcmp(channelnames[cnid], cn)); cnid++) ;
			if (channelnames[cnid] == NULL) cnid = -1;
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
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
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
		else {
			char *childcmd;
			char **childargs;
			int i = 0;

			childcmd = argv[argi];
			childargs = (char **) calloc((1 + argc - argi), sizeof(char *));
			while (argi < argc) { childargs[i++] = argv[argi++]; }
			addlocalpeer(childcmd, childargs);
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
	if (!locatorbased && (rbtBegin(peers) == rbtEnd(peers))) {
		errprintf("Must specify command for local worker\n");
		return 1;
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
			if (pidfile) fd = fopen(pidfile, "w");
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
	setup_signalhandler("hobbitd_channel");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
	signal(SIGALRM, SIG_IGN);

	/* Switch stdout/stderr to the logfile, if one was specified */
	freopen("/dev/null", "r", stdin);	/* hobbitd_channel's stdin is not used */
	if (logfn) {
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	/* Attach to the channel */
	channel = setup_channel(cnid, CHAN_CLIENT);
	if (channel == NULL) {
		errprintf("Channel not available\n");
		running = 0;
	}

	while (running) {
		/* 
		 * Wait for GOCLIENT to go up.
		 *
		 * Note that we use IPC_NOWAIT if there are messages in the
		 * queue, because then we just want to pick up a message if
		 * there is one, and if not we want to continue pushing the
		 * queued data to the worker.
		 */
		struct sembuf s;
		int n;
		time_t now = getcurrenttime(NULL);

		s.sem_num = GOCLIENT; s.sem_op  = -1; s.sem_flg = ((pendingcount > 0) ? IPC_NOWAIT : 0);
		n = semop(channel->semid, &s, 1);

		if (n == 0) {
			/*
			 * GOCLIENT went high, and so we got alerted about a new
			 * message arriving. Copy the message to our own buffer queue.
			 */
			char *inbuf = strdup(channel->channelbuf);

			/* 
			 * Now we have safely stored the new message in our buffer.
			 * Wait until any other clients on the same channel have picked up 
			 * this message (GOCLIENT reaches 0).
			 *
			 * We wrap this into an alarm handler, because it can occasionally
			 * fail, causing the whole system to lock up. We dont want that....
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

			/*
			 * See if they want us to rotate logs. We pass this on to
			 * the worker module as well, but must handle our own logfile.
			 */
			if (strncmp(inbuf, "@@logrotate", 11) == 0) {
				freopen(logfn, "a", stdout);
				freopen(logfn, "a", stderr);
			}

			/*
			 * Put the new message on our outbound queue.
			 */
			if (addmessage(inbuf) != 0) {
				/* Failed to queue message, free the buffer */
				xfree(inbuf);
			}
		}
		else {
			if (errno != EAGAIN) {
				dbgprintf("Semaphore wait aborted: %s\n", strerror(errno));
				continue;
			}
		}

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
		now = getcurrenttime(NULL);
		for (handle = rbtBegin(peers); (handle != rbtEnd(peers)); handle = rbtNext(peers, handle)) {
			int canwrite = 1, hasfailed = 0;
			hobbit_peer_t *pwalk;
			time_t msgtimeout = now - MSGTIMEOUT;
			int flushcount = 0;

			pwalk = (hobbit_peer_t *) gettreeitem(peers, handle);
			if (pwalk->msghead == NULL) continue; /* Ignore peers with nothing queued */

			switch (pwalk->peerstatus) {
			  case P_UP:
				canwrite = 1;
				break;

			  case P_DOWN:
				openconnection(pwalk);
				canwrite = (pwalk->peerstatus == P_UP);
				break;

			  case P_FAILED:
				canwrite = 0;
				break;
			}

			/* See if we have stale messages queued */
			while (pwalk->msghead && (pwalk->msghead->tstamp < msgtimeout)) {
				flushmessage(pwalk);
				flushcount++;
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
				tmo.tv_sec = 0; tmo.tv_usec = 2000;
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
	}

	/* Detach from channels */
	close_channel(channel, CHAN_CLIENT);

	/* Close peer connections */
	for (handle = rbtBegin(peers); (handle != rbtEnd(peers)); handle = rbtNext(peers, handle)) {
		hobbit_peer_t *pwalk = (hobbit_peer_t *) gettreeitem(peers, handle);
		shutdownconnection(pwalk);
	}

	/* Remove the PID file */
	if (pidfile) unlink(pidfile);

	return 0;
}

