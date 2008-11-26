/*----------------------------------------------------------------------------*/
/* Hobbit client message cache.                                               */
/*                                                                            */
/* This acts as a local network daemon which saves incoming messages in a     */
/* memory cache.                                                              */
/*                                                                            */
/* A "pullclient" command receives the cache content in response.             */
/* Any data provided in the "pullclient" request is saved, and passed as      */
/* response to the first "client" command seen afterwards.                    */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: msgcache.c,v 1.8 2006-07-20 16:06:41 henrik Exp $";

#include "config.h"

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

#include "version.h"
#include "libbbgen.h"

volatile int keeprunning = 1;
char *client_response = NULL;		/* The latest response to a "client" message */
char *logfile = NULL;
int maxage = 600;			/* Maximum time we will cache messages */
sender_t *serverlist = NULL;		/* Who is allowed to grab our messages */

typedef struct conn_t {
	time_t tstamp;
	struct sockaddr_in caddr;
	enum { C_CLIENT_CLIENT, C_CLIENT_OTHER, C_SERVER } ctype;
	enum { C_READING, C_WRITING, C_DONE } action;
	int sockfd;
	strbuffer_t *msgbuf;
	int sentbytes;
	struct conn_t *next;
} conn_t;
conn_t *chead = NULL;
conn_t *ctail = NULL;

typedef struct msgqueue_t {
	time_t tstamp;
	strbuffer_t *msgbuf;
	unsigned long sentto;
	struct msgqueue_t *next;
} msgqueue_t;
msgqueue_t *qhead = NULL;
msgqueue_t *qtail = NULL;


void sigmisc_handler(int signum)
{
	switch (signum) {
	  case SIGTERM:
		errprintf("Caught TERM signal, terminating\n");
		keeprunning = 0;
		break;

	  case SIGHUP:
		if (logfile) {
			freopen(logfile, "a", stdout);
			freopen(logfile, "a", stderr);
			errprintf("Caught SIGHUP, reopening logfile\n");
		}
		break;
	}
}

void grabdata(conn_t *conn)
{
	int n;
	char buf[8192];
	int pollid = 0;

	/* Get data from the connection socket - we know there is some */
	n = read(conn->sockfd, buf, sizeof(buf));
	if (n <= -1) {
		/* Read failure */
		errprintf("Connection lost during read: %s\n", strerror(errno));
		conn->action = C_DONE;
		return;
	}

	if (n > 0) {
		/* Got some data - store it */
		buf[n] = '\0';
		addtobuffer(conn->msgbuf, buf);
		return;
	}

	/* Done reading - process the data */
	if (STRBUFLEN(conn->msgbuf) == 0) {
		/* No data ? We're done */
		conn->action = C_DONE;
		return;
	}

	/* 
	 * See what kind of message this is. If it's a "pullclient" message,
	 * save the contents of the message - this is the client configuration
	 * that we'll return the next time a client sends us the "client" message.
	 */

	if (strncmp(STRBUF(conn->msgbuf), "pullclient", 10) == 0) {
		char *clientcfg;
		int idnum;

		/* Access check */
		if (!oksender(serverlist, NULL, conn->caddr.sin_addr, STRBUF(conn->msgbuf))) {
			errprintf("Rejected pullclient request from %s\n",
				  inet_ntoa(conn->caddr.sin_addr));
			conn->action = C_DONE;
			return;
		}

		dbgprintf("Got pullclient request: %s\n", STRBUF(conn->msgbuf));

		/*
		 * The pollid is unique for each Hobbit server. It is to allow
		 * multiple servers to pick up the same message, for resiliance.
		 */
		idnum = atoi(STRBUF(conn->msgbuf) + 10);
		if ((idnum <= 0) || (idnum > 31)) {
			pollid = 0;
		}
		else {
			pollid = (1 << idnum);
		}

		conn->ctype = C_SERVER;
		conn->action = C_WRITING;

		/* Save any client config sent to us */
		clientcfg = strchr(STRBUF(conn->msgbuf), '\n');
		if (clientcfg) {
			clientcfg++;
			if (client_response) xfree(client_response);
			client_response = strdup(clientcfg);
			dbgprintf("Saved client response: %s\n", client_response);
		}
	}
	else if (strncmp(STRBUF(conn->msgbuf), "client ", 7) == 0) {
		/*
		 * Got a "client" message. Return the client-response saved from
		 * earlier, if there is any. If not, then we're done.
		 */
		conn->ctype = C_CLIENT_CLIENT;
		conn->action = (client_response ? C_WRITING : C_DONE);
	}
	else {
		/* Message from a client, but not the "client" message. So no response. */
		conn->ctype = C_CLIENT_OTHER;
		conn->action = C_DONE;
	}

	/*
	 * Messages we receive from clients are stored on our outbound queue.
	 * If it's a local "client" message, respond with the queued response
	 * from the Hobbit server. Other client messages get no response.
	 *
	 * Server messages get our outbound queue back in response.
	 */
	if (conn->ctype != C_SERVER) {
		/* Messages from clients go on the outbound queue */
		msgqueue_t *newq = calloc(1, sizeof(msgqueue_t));
		dbgprintf("Queuing outbound message\n");
		newq->tstamp = conn->tstamp;
		newq->msgbuf = conn->msgbuf;
		conn->msgbuf = NULL;
		if (qtail) {
			qtail->next = newq;
			qtail = newq;
		}
		else {
			qhead = qtail = newq;
		}

		if ((conn->ctype == C_CLIENT_CLIENT) && (conn->action == C_WRITING)) {
			/* Send the response back to the client */
			conn->msgbuf = newstrbuffer(0);
			addtobuffer(conn->msgbuf, client_response);

			/* 
			 * Dont drop the client response data. If for some reason
			 * the "client" request is repeated, he should still get
			 * the right answer that we have.
			 */
		}
	}
	else {
		/* A server has asked us for our list of messages */
		time_t now = time(NULL);
		msgqueue_t *mwalk;

		if (!qhead) {
			/* No queued messages */
			conn->action = C_DONE;
		}
		else {
			/* Build a message of all the queued data */
			clearstrbuffer(conn->msgbuf);

			/* Index line first */
			for (mwalk = qhead; (mwalk); mwalk = mwalk->next) {
				if ((mwalk->sentto & pollid) == 0) {
					char idx[20];
					sprintf(idx, "%d:%ld ", 
						STRBUFLEN(mwalk->msgbuf), (long)(now - mwalk->tstamp));
					addtobuffer(conn->msgbuf, idx);
				}
			}

			if (STRBUFLEN(conn->msgbuf) > 0) addtobuffer(conn->msgbuf, "\n");

			/* Then the stream of messages */
			for (mwalk = qhead; (mwalk); mwalk = mwalk->next) {
				if ((mwalk->sentto & pollid) == 0) {
					if (pollid) mwalk->sentto |= pollid;
					addtostrbuffer(conn->msgbuf, mwalk->msgbuf);
				}
			}

			if (STRBUFLEN(conn->msgbuf) == 0) {
				/* No data for this server */
				conn->action = C_DONE;
			}
		}
	}
}

void senddata(conn_t *conn)
{
	int n, togo;
	char *startp;

	/* Send data on the connection socket */
	togo = STRBUFLEN(conn->msgbuf) - conn->sentbytes;
	startp = STRBUF(conn->msgbuf) + conn->sentbytes;
	n = write(conn->sockfd, startp, togo);

	if (n <= -1) {
		/* Write failure */
		errprintf("Connection lost during write to %s\n", inet_ntoa(conn->caddr.sin_addr));
		conn->action = C_DONE;
	}
	else {
		conn->sentbytes += n;
		if (conn->sentbytes == STRBUFLEN(conn->msgbuf)) conn->action = C_DONE;
	}
}


int main(int argc, char *argv[])
{
	int daemonize = 1;
	int listenq = 10;
	char *pidfile = "msgcache.pid";
	int lsocket;
	struct sockaddr_in laddr;
	struct sigaction sa;
	int opt;

	/* Dont save the output from errprintf() */
	save_errbuf = 0;

	memset(&laddr, 0, sizeof(laddr));
	inet_aton("0.0.0.0", (struct in_addr *) &laddr.sin_addr.s_addr);
	laddr.sin_port = htons(1984);
	laddr.sin_family = AF_INET;

	for (opt=1; (opt < argc); opt++) {
		if (argnmatch(argv[opt], "--listen=")) {
			char *locaddr, *p;
			int locport;

			locaddr = strchr(argv[opt], '=')+1;
			p = strchr(locaddr, ':');
			if (p) { locport = atoi(p+1); *p = '\0'; } else locport = 1984;

			memset(&laddr, 0, sizeof(laddr));
			laddr.sin_port = htons(locport);
			laddr.sin_family = AF_INET;
			if (inet_aton(locaddr, (struct in_addr *) &laddr.sin_addr.s_addr) == 0) {
				errprintf("Invalid listen address %s\n", locaddr);
				return 1;
			}
		}
		else if (argnmatch(argv[opt], "--server=")) {
			/* Who is allowed to fetch cached messages */
			char *p = strchr(argv[opt], '=');
			serverlist = getsenderlist(p+1);
		}
		else if (argnmatch(argv[opt], "--max-age=")) {
			char *p = strchr(argv[opt], '=');
			maxage = atoi(p+1);
		}
		else if (argnmatch(argv[opt], "--lqueue=")) {
			char *p = strchr(argv[opt], '=');
			listenq = atoi(p+1);
		}
		else if (strcmp(argv[opt], "--daemon") == 0) {
			daemonize = 1;
		}
		else if (strcmp(argv[opt], "--no-daemon") == 0) {
			daemonize = 0;
		}
		else if (argnmatch(argv[opt], "--pidfile=")) {
			char *p = strchr(argv[opt], '=');
			pidfile = strdup(p+1);
		}
		else if (argnmatch(argv[opt], "--logfile=")) {
			char *p = strchr(argv[opt], '=');
			logfile = strdup(p+1);
		}
		else if (strcmp(argv[opt], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[opt], "--version") == 0) {
			printf("bbproxy version %s\n", VERSION);
			return 0;
		}
	}

	/* Set up a socket to listen for new connections */
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

	/* Redirect logging to the logfile, if requested */
	if (logfile) {
		freopen(logfile, "a", stdout);
		freopen(logfile, "a", stderr);
	}

	errprintf("Hobbit msgcache version %s starting\n", VERSION);
	errprintf("Listening on %s:%d\n", inet_ntoa(laddr.sin_addr), ntohs(laddr.sin_port));

	if (daemonize) {
		pid_t childpid;

		freopen("/dev/null", "a", stdin);

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

	setup_signalhandler("msgcache");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigmisc_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	do {
		fd_set fdread, fdwrite;
		int maxfd;
		int n;
		conn_t *cwalk, *cprev;
		msgqueue_t *qwalk, *qprev;
		time_t mintstamp;

		/* Remove any finished connections */
		cwalk = chead; cprev = NULL;
		while (cwalk) {
			conn_t *zombie;

			if (cwalk->action != C_DONE) {
				cprev = cwalk;
				cwalk = cwalk->next;
				continue;
			}

			/* Close the socket */
			close(cwalk->sockfd);

			zombie = cwalk;
			if (cprev == NULL) {
				chead = zombie->next;
				cwalk = chead;
				cprev = NULL;
			}
			else {
				cprev->next = zombie->next;
				cwalk = zombie->next;
			}

			freestrbuffer(zombie->msgbuf);
			xfree(zombie);
		}
		ctail = chead;
		if (ctail) { while (ctail->next) ctail = ctail->next; }


		/* Remove expired messages */
		qwalk = qhead; qprev = NULL;
		mintstamp = time(NULL) - maxage;
		while (qwalk) {
			msgqueue_t *zombie;

			if (qwalk->tstamp > mintstamp) {
				/* Hasn't expired yet */
				qprev = qwalk;
				qwalk = qwalk->next;
				continue;
			}

			zombie = qwalk;
			if (qprev == NULL) {
				qhead = zombie->next;
				qwalk = qhead;
				qprev = NULL;
			}
			else {
				qprev->next = zombie->next;
				qwalk = zombie->next;
			}

			freestrbuffer(zombie->msgbuf);
			xfree(zombie);
		}
		qtail = qhead;
		if (qtail) { while (qtail->next) qtail = qtail->next; }


		/* Now we're ready to handle some data */
		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);

		/* Add the listen socket */
		FD_SET(lsocket, &fdread); 
		maxfd = lsocket;

		for (cwalk = chead; (cwalk); cwalk = cwalk->next) {
			switch (cwalk->action) {
			  case C_READING:
				FD_SET(cwalk->sockfd, &fdread); 
				if (cwalk->sockfd > maxfd) maxfd = cwalk->sockfd; 
				break;

			  case C_WRITING:
				FD_SET(cwalk->sockfd, &fdwrite);
				if (cwalk->sockfd > maxfd) maxfd = cwalk->sockfd; 
				break;

			  case C_DONE:
				break;
			}
		}

		n = select(maxfd+1, &fdread, &fdwrite, NULL, NULL);

		if (n < 0) {
			if (errno == EINTR) continue;
			errprintf("select failed: %s\n", strerror(errno));
			return 0;
		}

		if (n == 0) continue; /* Timeout */

		for (cwalk = chead; (cwalk); cwalk = cwalk->next) {
			switch (cwalk->action) {
			  case C_READING:
				if (FD_ISSET(cwalk->sockfd, &fdread)) grabdata(cwalk);
				break;

			  case C_WRITING:
				if (FD_ISSET(cwalk->sockfd, &fdwrite)) senddata(cwalk);
				break;

			  case C_DONE:
				break;
			}
		}

		if (FD_ISSET(lsocket, &fdread)) {
			/* New incoming connection */
			conn_t *newconn;
			int caddrsize;

			dbgprintf("New connection\n");
			newconn = calloc(1, sizeof(conn_t));

			caddrsize = sizeof(newconn->caddr);
			newconn->sockfd = accept(lsocket, (struct sockaddr *)&newconn->caddr, &caddrsize);
			if (newconn->sockfd == -1) {
				/* accept() failure. Yes, it does happen! */
				dbgprintf("accept failure, ignoring connection (%s)\n", strerror(errno));
				xfree(newconn);
				newconn = NULL;
			}
			else {
				fcntl(newconn->sockfd, F_SETFL, O_NONBLOCK);
				newconn->action = C_READING;
				newconn->msgbuf = newstrbuffer(0);
				newconn->tstamp = time(NULL);
			}

			if (newconn) {
				if (ctail) { ctail->next = newconn; ctail = newconn; }
				else chead = ctail = newconn;
			}
		}

	} while (keeprunning);

	if (pidfile) unlink(pidfile);
	return 0;
}

