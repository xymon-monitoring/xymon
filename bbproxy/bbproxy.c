/*----------------------------------------------------------------------------*/
/* Big Brother message proxy.                                                 */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbproxy.c,v 1.9 2004-09-19 11:34:42 henrik Exp $";

#include <sys/time.h>
#include <sys/types.h>
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

#include "bbgen.h"
#include "util.h"
#include "debug.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

static const char *VERSION_STRING = "1.0";

enum phase_t {
	P_IDLE, 

	P_REQ_READING,		/* Reading request data */
	P_REQ_READY, 		/* Done reading request from client */

	P_REQ_CONNECTING,	/* Connecting to server */
	P_REQ_SENDING, 		/* Sending request data */
	P_REQ_DONE,		/* Done sending request data to server */

	P_RESP_READING, 
	P_RESP_READY,
	P_RESP_SENDING, 
	P_RESP_DONE,
	P_CLEANUP
};

char *statename[] = {
	"idle",
	"req_reading",
	"req_ready",
	"req_connecting",
	"req_sending",
	"req_done",
	"resp_reading",
	"resp_ready",
	"resp_sending",
	"resp_done",
	"cleanup"
};

typedef struct conn_t {
	enum phase_t state;
	int csocket;
	struct sockaddr caddr;
	int ssocket;
	int conntries;
	time_t conntime;
	unsigned char *buf, *bufp;
	unsigned int bufsize, buflen;
	struct conn_t *next;
} conn_t;

#define CONNECT_TRIES 3		/* How many connect-attempts against the server */
#define CONNECT_INTERVAL 8	/* seconds between each connection attempt */
#define BUFSZ_READ 2048
#define BUFSZ_INC  8192
#define MAX_OPEN_SOCKS 256

static void do_read(int sockfd, conn_t *conn, enum phase_t completedstate)
{
	int n;

	if ((conn->buflen + BUFSZ_READ + 1) > conn->bufsize) {
		conn->bufsize += BUFSZ_INC;
		conn->buf = realloc(conn->buf, conn->bufsize);
		conn->bufp = conn->buf + conn->buflen;
	}

	n = read(sockfd, conn->bufp, (conn->bufsize - conn->buflen - 1));
	if (n == -1) {
		/* Error - abort */
		errprintf("READ error : %s\n", strerror(errno));
		conn->state = P_CLEANUP;
	}
	else if (n == 0) {
		/* EOF - request is complete */
		conn->state = completedstate;
	}
	else {
		conn->buflen += n;
		conn->bufp += n;
		*conn->bufp = '\0';
	}
}

static void do_write(int sockfd, conn_t *conn, enum phase_t completedstate)
{
	int n;

	n = write(sockfd, conn->bufp, conn->buflen);
	if (n == -1) {
		/* Error - abort */
		errprintf("WRITE error : %s\n", strerror(errno));
		conn->state = P_CLEANUP;
	}
	else if (n > 0) { 
		conn->buflen -= n; 
		conn->bufp += n; 
		if (conn->buflen == 0) {
			conn->state = completedstate;
		}
	}
}

int main(int argc, char *argv[])
{
	int locport = 1984;
	char *locaddr = "0.0.0.0";
	int remport = 1984;
	char *remaddr = NULL;
	int daemonize = 1;
	int timeout = 10;
	int listenq = 512;

	int sockcount = 0;
	int lsocket;
	struct sockaddr_in laddr;
	struct sockaddr_in saddr;
	int opt;

	conn_t *chead = NULL;
	save_errbuf = 0;

	for (opt=1; (opt < argc); opt++) {
		if (argnmatch(argv[opt], "--local=")) {
			char *p = strchr(argv[opt], '=');
			locaddr = strdup(p+1);
			p = strchr(locaddr, ':');
			if (p) {
				*p = '\0';
				locport = atoi(p+1);
			}
		}
		else if (argnmatch(argv[opt], "--remote=")) {
			char *p = strchr(argv[opt], '=');
			remaddr = strdup(p+1);
			p = strchr(remaddr, ':');
			if (p) {
				*p = '\0';
				remport = atoi(p+1);
			}
		}
		else if (argnmatch(argv[opt], "--timeout=")) {
			char *p = strchr(argv[opt], '=');
			timeout = atoi(p+1);
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
		else if (strcmp(argv[opt], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[opt], "--version") == 0) {
			printf("bbproxy version %s\n", VERSION_STRING);
			return 0;
		}
		else if (strcmp(argv[opt], "--help") == 0) {
			printf("bbproxy version %s\n", VERSION_STRING);
			printf("\nOptions:\n");
			printf("\t--local=IP[:port]           : Listen address and portnumber\n");
			printf("\t--remote=IP[:port]          : Server address and portnumber\n");
			printf("\t--timeout=N                 : Communications timeout (seconds)\n");
			printf("\t--lqueue=N                  : Listen-queue size\n");
			printf("\t--daemon                    : Run as a daemon\n");
			printf("\t--no-daemon                 : Do not run as a daemon\n");
			printf("\t--debug                     : Enable debugging output\n");
			printf("\n");
			return 0;
		}
	}

	if (remaddr == NULL) {
		errprintf("No remote address given - aborting\n");
		return 1;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_port = htons(remport);
	saddr.sin_family = AF_INET;
	if (inet_aton(remaddr, (struct in_addr *) &saddr.sin_addr.s_addr) == 0) {
		errprintf("Invalid remote address %s\n", remaddr);
		return 1;
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
	memset(&laddr, 0, sizeof(laddr));
	laddr.sin_port = htons(locport);
	laddr.sin_family = AF_INET;
	if (inet_aton(locaddr, (struct in_addr *) &laddr.sin_addr.s_addr) == 0) {
		errprintf("Invalid listen address %s\n", locaddr);
		return 1;
	}
	if (bind(lsocket, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
		errprintf("Cannot bind to listen socket (%s)\n", strerror(errno));
		return 1;
	}

	if (listen(lsocket, listenq) == -1) {
		errprintf("Cannot listen (%s)\n", strerror(errno));
		return 1;
	}

	errprintf("Listening on %s port %d\n", locaddr, locport);
	errprintf("Sending to %s port %d\n", remaddr, remport);

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
			/* Parent - just exit */
			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	signal(SIGPIPE, SIG_IGN);

	do {
		fd_set fdread, fdwrite;
		int maxfd;
		struct timeval tmo;
		int n, idx;
		conn_t *cwalk;
		time_t ctime;

		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);
		maxfd = -1;

		for (cwalk = chead, idx=0; (cwalk); cwalk = cwalk->next, idx++) {
			dprintf("state %d: %s\n", idx, statename[cwalk->state]);

			switch (cwalk->state) {
			  case P_REQ_READING:
				FD_SET(cwalk->csocket, &fdread); 
				if (cwalk->csocket > maxfd) maxfd = cwalk->csocket; 
				break;

			  case P_REQ_READY:
				shutdown(cwalk->csocket, SHUT_RD);
				cwalk->bufp = cwalk->buf;
				cwalk->state = P_REQ_CONNECTING;
				cwalk->conntries = CONNECT_TRIES;
				cwalk->conntime = 0;
				/* Fall through */

			  case P_REQ_CONNECTING:
				ctime = time(NULL);
				if (ctime < (cwalk->conntime + CONNECT_INTERVAL)) {
					dprintf("Delaying retry of connection\n");
					break;
				}

				cwalk->conntries--;
				if (cwalk->conntries < 0) {
					errprintf("Could not connect to server\n");
					cwalk->state = P_CLEANUP;
					break;
				}

				cwalk->ssocket = socket(AF_INET, SOCK_STREAM, 0);
				if (cwalk->ssocket == -1) {
					errprintf("Could not get a socket - will try again\n");
					break; /* Retry the next time around */
				}
				sockcount++;
				fcntl(cwalk->ssocket, F_SETFL, O_NONBLOCK);

				n = connect(cwalk->ssocket, (struct sockaddr *)&saddr, sizeof(saddr));
				if ((n == 0) || ((n == -1) && (errno == EINPROGRESS))) {
					cwalk->state = P_REQ_SENDING;
					/* Fallthrough */
				}
				else {
					/* Could not connect! Invoke retries */
					errprintf("Connect to server failed: %s\n", strerror(errno));
					close(cwalk->ssocket); sockcount--;
					cwalk->ssocket = -1;
					break;
				}
				/* No "break" here! */
			  
			  case P_REQ_SENDING:
				FD_SET(cwalk->ssocket, &fdwrite); 
				if (cwalk->ssocket > maxfd) maxfd = cwalk->ssocket;
				break;

			  case P_REQ_DONE:
				/* Request is off to the server */
				shutdown(cwalk->ssocket, SHUT_WR);
				cwalk->bufp = cwalk->buf; cwalk->buflen = 0;
				memset(cwalk->buf, 0, cwalk->bufsize);
				cwalk->state = P_RESP_READING;
				/* Fallthrough */

			  case P_RESP_READING:
				FD_SET(cwalk->ssocket, &fdread); 
				if (cwalk->ssocket > maxfd) maxfd = cwalk->ssocket;
				break;

			  case P_RESP_READY:
				shutdown(cwalk->ssocket, SHUT_RD);
				close(cwalk->ssocket); sockcount--;
				cwalk->ssocket = -1;
				cwalk->bufp = cwalk->buf;
				cwalk->state = P_RESP_SENDING;
				/* Fall through */

			  case P_RESP_SENDING:
				if (cwalk->buflen) {
					FD_SET(cwalk->csocket, &fdwrite);
					if (cwalk->csocket > maxfd) maxfd = cwalk->csocket;
					break;
				}
				else {
					cwalk->state = P_RESP_DONE;
				}
				/* Fall through */

			  case P_RESP_DONE:
				shutdown(cwalk->csocket, SHUT_WR);
				close(cwalk->csocket); sockcount--;
				cwalk->csocket = -1;
				cwalk->state = P_CLEANUP;
				/* Fall through */

			  case P_CLEANUP:
				if (cwalk->csocket >= 0) {
					close(cwalk->csocket); sockcount--;
					cwalk->csocket = -1;
				}
				if (cwalk->ssocket >= 0) {
					close(cwalk->ssocket); sockcount--;
					cwalk->ssocket = -1;
				}
				cwalk->bufp = cwalk->bufp; 
				cwalk->buflen = 0;
				memset(cwalk->buf, 0, cwalk->bufsize);
				memset(&cwalk->caddr, 0, sizeof(cwalk->caddr));
				cwalk->state = P_IDLE;
				break;

			  case P_IDLE:
				break;

			  default:
				break;
			}
		}

		/* Add the listen-socket to the select() list, but only if we have room */
		if (sockcount < MAX_OPEN_SOCKS) {
			FD_SET(lsocket, &fdread); 
			if (lsocket > maxfd) maxfd = lsocket;
		}
		else {
			static time_t lastlogging = 0;
			time_t now;
			if ((now = time(NULL)) != lastlogging) {
				lastlogging = now;
				errprintf("Squelching incoming connections, sockcount=%d\n", sockcount);
			}
		}

		tmo.tv_sec = timeout;
		tmo.tv_usec = 0;
		n = select(maxfd+1, &fdread, &fdwrite, NULL, &tmo);
		if (n <= 0) {
			for (cwalk = chead; (cwalk); cwalk = cwalk->next) {
				switch (cwalk->state) {
				  case P_IDLE:
					break;

				  default: 
					cwalk->state = P_CLEANUP;
					break;
				}
			}
		}
		else {
			for (cwalk = chead; (cwalk); cwalk = cwalk->next) {
				switch (cwalk->state) {
				  case P_REQ_READING:
					if (FD_ISSET(cwalk->csocket, &fdread)) {
						do_read(cwalk->csocket, cwalk, P_REQ_READY);
					}
					break;

				  case P_REQ_SENDING:
					if (FD_ISSET(cwalk->ssocket, &fdwrite)) {
						if (cwalk->bufp == cwalk->buf) {
							int connres, connressize;

							/* First time ready for write - check connect status */
							connressize = sizeof(connres);
							n = getsockopt(cwalk->ssocket, SOL_SOCKET, SO_ERROR, &connres, &connressize);
							if (connres != 0) {
								/* Connect failed! Invoke retries. */
								errprintf("Connect to server failed: %s - retrying\n", 
										strerror(errno));
								close(cwalk->ssocket); sockcount--;
								cwalk->ssocket = -1;
								cwalk->state = P_REQ_CONNECTING;
								break;
							}
						}

						do_write(cwalk->ssocket, cwalk, P_REQ_DONE);
					}
					break;

				  case P_RESP_READING:
					if (FD_ISSET(cwalk->ssocket, &fdread)) {
						do_read(cwalk->ssocket, cwalk, P_RESP_READY);
					}
					break;

				  case P_RESP_SENDING:
					if (FD_ISSET(cwalk->csocket, &fdwrite)) {
						do_write(cwalk->csocket, cwalk, P_RESP_DONE);
					}
					break;

				  default:
					break;
				}
			}

			if (FD_ISSET(lsocket, &fdread)) {
				/* New incoming connection */
				conn_t *newconn;
				int caddrsize;

				dprintf("New connection\n");
				for (cwalk = chead; (cwalk && (cwalk->state != P_IDLE)); cwalk = cwalk->next);
				if (cwalk) {
					newconn = cwalk;
				}
				else {
					newconn = malloc(sizeof(conn_t));
					newconn->next = chead;
					chead = newconn;
					newconn->bufsize = BUFSZ_INC;
					newconn->buf = newconn->bufp = malloc(newconn->bufsize);
				}

				newconn->ssocket = -1;
				newconn->conntries = 0;
				newconn->buflen = 0;
				*newconn->buf = '\0';

				caddrsize = sizeof(newconn->caddr);
				newconn->csocket = accept(lsocket, (struct sockaddr *)&newconn->caddr, &caddrsize);
				if (newconn->csocket == -1) {
					/* accept() failure. Yes, it does happen! */
					errprintf("accept failure, ignoring connection (%s), sockcount=%d\n", 
						strerror(errno), sockcount);
					newconn->state = P_IDLE;
				}
				else {
					sockcount++;
					fcntl(newconn->csocket, F_SETFL, O_NONBLOCK);
					newconn->state = P_REQ_READING;
				}
			}
		}
	} while (1);
}

