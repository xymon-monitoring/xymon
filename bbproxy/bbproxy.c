/*----------------------------------------------------------------------------*/
/* Big Brother message proxy.                                                 */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbproxy.c,v 1.1 2004-09-18 22:27:46 henrik Exp $";

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

enum phase_t {
	P_IDLE, 

	P_REQ_NEW,		/* New incoming connection */
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

typedef struct conn_t {
	enum phase_t state;
	int csocket;
	struct sockaddr caddr;
	int ssocket;
	int conntries;
	unsigned char *buf, *bufp;
	unsigned int buflen;
	struct conn_t *next;
} conn_t;

#define CONNECT_TRIES 5
conn_t *chead = NULL;

static char *statename(enum phase_t state)
{
	switch (state) {
	  case P_IDLE: return "idle";
	  case P_REQ_NEW: return "req_new";
	  case P_REQ_READING: return "req_reading";
	  case P_REQ_READY: return "req_ready";
	  case P_REQ_CONNECTING: return "req_connecting";
	  case P_REQ_SENDING: return "req_sending";
	  case P_REQ_DONE: return "req_done";
	  case P_RESP_READING: return "resp_reading";
	  case P_RESP_READY: return "resp_ready";
	  case P_RESP_SENDING: return "resp_sending";
	  case P_RESP_DONE: return "resp_done";
	  case P_CLEANUP: return "cleanup";
	}
}

int main(int argc, char *argv[])
{
	int localport = 1984;
	int remport = 1984;
	char *remaddr = "172.16.10.2";
	int timeout = 10;
	int listenq = 20;

	pid_t childpid;
	int lsocket;
	struct sockaddr_in laddr;
	struct sockaddr_in saddr;
	int opt;

	/* Set up a socket to listen for new connections */
	lsocket = socket(AF_INET, SOCK_STREAM, 0);
	opt = 1;
	setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	memset(&laddr, 0, sizeof(laddr));
	laddr.sin_port = htons(localport);
	laddr.sin_family = AF_INET;
	bind(lsocket, (struct sockaddr *)&laddr, sizeof(laddr));
	listen(lsocket, listenq);

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_port = htons(remport);
	saddr.sin_family = AF_INET;
	inet_aton(remaddr, (struct in_addr *) &saddr.sin_addr.s_addr);

	/* Become a daemon */
	childpid = fork();
	if (childpid < 0) {
		exit(1);
	}
	else if (childpid > 0) {
		exit(0);
	}
	/* Child (daemon) continues here */
	setsid();

	signal(SIGPIPE, SIG_IGN);

	do {
		fd_set fdread, fdwrite;
		int maxfd;
		struct timeval tmo;
		int n, idx;
		conn_t *cwalk;
		unsigned char mbuf[32768];

		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);

		FD_SET(lsocket, &fdread); maxfd = lsocket;
		for (cwalk = chead, idx=0; (cwalk); cwalk = cwalk->next, idx++) {
			printf("state %d: %s\n", idx, statename(cwalk->state));

			switch (cwalk->state) {
			  case P_REQ_READING:
				FD_SET(cwalk->csocket, &fdread); 
				if (cwalk->csocket > maxfd) maxfd = cwalk->csocket; 
				break;

			  case P_REQ_READY:
				/* We have the request, try getting a socket for the server connection */
				shutdown(cwalk->csocket, SHUT_RD);
				cwalk->bufp = cwalk->buf;
				cwalk->state = P_REQ_CONNECTING;
				cwalk->conntries = CONNECT_TRIES;
				/* Fall through */

			  case P_REQ_CONNECTING:
				cwalk->ssocket = socket(AF_INET, SOCK_STREAM, 0);
				if (cwalk->ssocket == -1) break; /* Retry the next time around */
				fcntl(cwalk->ssocket, F_SETFL, O_NONBLOCK);

				cwalk->conntries--;
				n = connect(cwalk->ssocket, (struct sockaddr *)&saddr, sizeof(saddr));
				if ((n == 0) || ((n == -1) && (errno == EINPROGRESS))) {
					cwalk->state = P_REQ_SENDING;
					/* Fallthrough */
				}
				else {
					/* Could not connect! */
					cwalk->state = (cwalk->conntries ? P_REQ_CONNECTING : P_CLEANUP);
					break;
				}
			  
			  case P_REQ_SENDING:
				FD_SET(cwalk->ssocket, &fdwrite); 
				if (cwalk->ssocket > maxfd) maxfd = cwalk->ssocket;
				break;

			  case P_REQ_DONE:
				/* Request is off to the server */
				shutdown(cwalk->ssocket, SHUT_WR);
				if (cwalk->buf) free(cwalk->buf);
				cwalk->buf = cwalk->bufp = NULL; cwalk->buflen = 0;
				cwalk->state = P_RESP_READING;
				/* Fallthrough */

			  case P_RESP_READING:
				FD_SET(cwalk->ssocket, &fdread); 
				if (cwalk->ssocket > maxfd) maxfd = cwalk->ssocket;
				break;

			  case P_RESP_READY:
				shutdown(cwalk->ssocket, SHUT_RD);
				close(cwalk->ssocket);
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
				cwalk->state = P_CLEANUP;
				/* Fall through */

			  case P_CLEANUP:
				if (cwalk->csocket >= 0) {
					close(cwalk->csocket);
					cwalk->csocket = -1;
				}
				if (cwalk->ssocket >= 0) {
					close(cwalk->ssocket);
					cwalk->ssocket = -1;
				}
				if (cwalk->buf) free(cwalk->buf);
				cwalk->buf = cwalk->bufp = NULL; cwalk->buflen = 0;
				memset(&cwalk->caddr, 0, sizeof(cwalk->caddr));
				cwalk->state = P_IDLE;
				break;
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
						n = read(cwalk->csocket, mbuf, sizeof(mbuf)-1);
						if (n == -1) {
							/* Error - abort */
							cwalk->state = P_CLEANUP;
						}
						else if (n == 0) {
							/* EOF - request is complete */
							cwalk->state = P_REQ_READY;
						}
						else {
							mbuf[n] = '\0';
							cwalk->buflen += strlen(mbuf);
							if (cwalk->buf == NULL) {
								cwalk->buf = strdup(mbuf);
							}
							else {
								cwalk->buf = realloc(cwalk->buf, cwalk->buflen+1);
								strcat(cwalk->buf, mbuf);
							}
						}
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
								/* Connect failed! */
								cwalk->state = P_RESP_DONE;
								break;
							}
						}

						n = write(cwalk->ssocket, cwalk->bufp, cwalk->buflen);
						if (n < 0) {
							cwalk->state = P_CLEANUP;
						}
						else if (n > 0) { 
							cwalk->buflen -= n; 
							cwalk->bufp += n; 
							if (cwalk->buflen == 0) {
								cwalk->state = P_REQ_DONE;
							}
						}
					}
					break;

				  case P_RESP_READING:
					if (FD_ISSET(cwalk->ssocket, &fdread)) {
						n = read(cwalk->ssocket, mbuf, sizeof(mbuf)-1);
						if (n == -1) {
							/* Error - abort */
							cwalk->state = P_CLEANUP;
						}
						else if (n == 0) {
							/* EOF - response is complete */
							cwalk->state = P_RESP_READY;
						}
						else {
							mbuf[n] = '\0';
							cwalk->buflen += strlen(mbuf);
							if (cwalk->buf == NULL) {
								cwalk->buf = strdup(mbuf);
							}
							else {
								cwalk->buf = realloc(cwalk->buf, cwalk->buflen+1);
								strcat(cwalk->buf, mbuf);
							}
						}
					}
					break;

				  case P_RESP_SENDING:
					if (FD_ISSET(cwalk->csocket, &fdwrite)) {
						n = write(cwalk->csocket, cwalk->bufp, cwalk->buflen);
						if (n == -1) {
							/* Error - abort */
							cwalk->state = P_CLEANUP;
						}
						else if (n > 0) { 
							cwalk->buflen -= n; 
							cwalk->bufp += n; 
							if (cwalk->buflen == 0) {
								cwalk->state = P_RESP_DONE;
							}
						}
					}
					break;
				}
			}

			if (FD_ISSET(lsocket, &fdread)) {
				/* New incoming connection */
				conn_t *newconn;
				int caddrsize = sizeof(newconn->caddr);

				printf("New connection\n");
				for (cwalk = chead; (cwalk && (cwalk->state != P_IDLE)); cwalk = cwalk->next);
				if (cwalk) {
					memset(cwalk, 0, sizeof(conn_t));
					newconn = cwalk;
				}
				else {
					newconn = calloc(1, sizeof(conn_t));
					newconn->next = chead;
					chead = newconn;
				}

				newconn->state = P_REQ_NEW;
				newconn->csocket = accept(lsocket, (struct sockaddr *)&newconn->caddr, &caddrsize);
				fcntl(newconn->csocket, F_SETFL, O_NONBLOCK);
				newconn->ssocket = -1;
				newconn->buf = newconn->bufp = NULL;
				newconn->buflen = 0;
				newconn->state = P_REQ_READING;
			}
		}
	} while (1);
}

