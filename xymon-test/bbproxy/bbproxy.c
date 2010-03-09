/*----------------------------------------------------------------------------*/
/* Hobbit message proxy.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbproxy.c 6125 2009-02-12 13:09:34Z storner $";

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

enum phase_t {
	P_IDLE, 

	P_REQ_READING,		/* Reading request data */
	P_REQ_READY, 		/* Done reading request from client */

	P_REQ_COMBINING,

	P_REQ_CONNECTING,	/* Connecting to server */
	P_REQ_SENDING, 		/* Sending request data */
	P_REQ_DONE,		/* Done sending request data to server */

	P_RESP_READING, 
	P_RESP_READY,
	P_RESP_SENDING, 
	P_RESP_DONE,
	P_CLEANUP
};

char *statename[P_CLEANUP+1] = {
	"idle",
	"reading from client",
	"request from client OK",
	"request combining",
	"connecting to server",
	"sending to server",
	"request sent",
	"reading from server",
	"response from server OK",
	"sending to client",
	"response sent",
	"cleanup"
};

typedef struct conn_t {
	enum phase_t state;
	int csocket;
	struct sockaddr_in caddr;
	struct in_addr *clientip, *serverip;
	int snum;
	int ssocket;
	int conntries, sendtries;
	int connectpending;
	time_t conntime;
	int madetocombo;
	struct timespec arrival;
	struct timespec timelimit;
	unsigned char *buf, *bufp, *bufpsave;
	unsigned int bufsize, buflen, buflensave;
	struct conn_t *next;
} conn_t;

#define MAX_SERVERS 3
#define CONNECT_TRIES 3		/* How many connect-attempts against the server */
#define CONNECT_INTERVAL 8	/* Seconds between each connection attempt */
#define SEND_TRIES 2		/* How many times to try sending a message */
#define BUFSZ_READ 2048		/* Minimum #bytes that must be free when read'ing into a buffer */
#define BUFSZ_INC  8192		/* How much to grow the buffer when it is too small */
#define MAX_OPEN_SOCKS 256
#define MINIMUM_FOR_COMBO 2048	/* To start merging messages, at least have 2 KB free */
#define MAXIMUM_FOR_COMBO 32768 /* Max. size of a combined message */
#define COMBO_DELAY 250000000	/* Delay before sending a combo message (in nanoseconds) */

int keeprunning = 1;
time_t laststatus = 0;
char *logfile = NULL;
int logdetails = 0;
unsigned long msgs_timeout_from[P_CLEANUP+1] = { 0, };


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

	  case SIGUSR1:
		/* Toggle logging of details */
		logdetails = (1 - logdetails);
		errprintf("Log details is %sabled\n", (logdetails ? "en" : "dis"));
		break;
	}
}

int overdue(struct timespec *now, struct timespec *limit)
{
	if (now->tv_sec < limit->tv_sec) return 0;
	else if (now->tv_sec > limit->tv_sec) return 1;
	else return (now->tv_nsec >= limit->tv_nsec);
}

static int do_read(int sockfd, struct in_addr *addr, conn_t *conn, enum phase_t completedstate)
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
		errprintf("READ error from %s: %s\n", inet_ntoa(*addr), strerror(errno));
		msgs_timeout_from[conn->state]++;
		conn->state = P_CLEANUP;
		return -1;
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

	return 0;
}

static int do_write(int sockfd, struct in_addr *addr, conn_t *conn, enum phase_t completedstate)
{
	int n;

	n = write(sockfd, conn->bufp, conn->buflen);
	if (n == -1) {
		/* Error - abort */
		errprintf("WRITE error to %s: %s\n", inet_ntoa(*addr), strerror(errno));
		msgs_timeout_from[conn->state]++;
		conn->state = P_CLEANUP;
		return -1;
	}
	else if (n >= 0) { 
		conn->buflen -= n; 
		conn->bufp += n; 
		if (conn->buflen == 0) {
			conn->state = completedstate;
		}
	}

	return 0;
}

void do_log(conn_t *conn)
{
	char *rq, *eol, *delim;
	char savechar;

	rq = conn->buf+6;
	if (strncmp(rq, "combo\n", 6) == 0) rq += 6;

	eol = strchr(rq, '\n'); if (eol) *eol = '\0';
	for (delim = rq; (*delim && isalpha((unsigned char) *delim)); delim++);
	for (; (*delim && isspace((unsigned char) *delim)); delim++);
	for (; (*delim && !isspace((unsigned char) *delim)); delim++);
	savechar = *delim; *delim = '\0';

	errprintf("%s : %s\n", inet_ntoa(*conn->clientip), rq);
	*delim = savechar;
	if (eol) *eol = '\n';
}

int main(int argc, char *argv[])
{
	int daemonize = 1;
	int timeout = 10;
	int listenq = 512;
	char *pidfile = "/var/run/bbproxy.pid";
	char *proxyname = NULL;
	char *proxynamesvc = "bbproxy";

	int sockcount = 0;
	int lsocket;
	struct sockaddr_in laddr;
	struct sockaddr_in bbdispaddr[MAX_SERVERS];
	int bbdispcount = 0;
	struct sockaddr_in bbpageraddr[MAX_SERVERS];
	int bbpagercount = 0;
	int usehobbitd = 0;
	int opt;
	conn_t *chead = NULL;
	struct sigaction sa;

	/* Statistics info */
	time_t startuptime = gettimer();
	unsigned long msgs_total = 0;
	unsigned long msgs_total_last = 0;
	unsigned long msgs_combined = 0;
	unsigned long msgs_merged = 0;
	unsigned long msgs_delivered = 0;
	unsigned long msgs_status = 0;
	unsigned long msgs_page = 0;
	unsigned long msgs_combo = 0;
	unsigned long msgs_other = 0;
	unsigned long msgs_recovered = 0;
	struct timespec timeinqueue = { 0, 0 };

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
		else if (argnmatch(argv[opt], "--bbdisplay=")) {
			char *ips, *ip1;
			int port1;

			ips = strdup(strchr(argv[opt], '=')+1);

			ip1 = strtok(ips, ",");
			while (ip1) {
				char *p; 
				p = strchr(ip1, ':');
				if (p) { port1 = atoi(p+1); *p = '\0'; } else port1 = 1984;

				memset(&bbdispaddr[bbdispcount], 0, sizeof(bbdispaddr[bbdispcount]));
				bbdispaddr[bbdispcount].sin_port = htons(port1);
				bbdispaddr[bbdispcount].sin_family = AF_INET;
				if (inet_aton(ip1, (struct in_addr *) &bbdispaddr[bbdispcount].sin_addr.s_addr) == 0) {
					errprintf("Invalid remote address %s\n", ip1);
				}
				else {
					bbdispcount++;
				}
				if (p) *p = ':';
				ip1 = strtok(NULL, ",");
			}
			xfree(ips);
		}
		else if (argnmatch(argv[opt], "--bbpager=")) {
			char *ips, *ip1;
			int port1;

			ips = strdup(strchr(argv[opt], '=')+1);

			ip1 = strtok(ips, ",");
			while (ip1) {
				char *p; 
				p = strchr(ip1, ':');
				if (p) { port1 = atoi(p+1); *p = '\0'; } else port1 = 1984;

				memset(&bbpageraddr[bbpagercount], 0, sizeof(bbpageraddr[bbpagercount]));
				bbpageraddr[bbpagercount].sin_port = htons(port1);
				bbpageraddr[bbpagercount].sin_family = AF_INET;
				if (inet_aton(ip1, (struct in_addr *) &bbpageraddr[bbpagercount].sin_addr.s_addr) == 0) {
					errprintf("Invalid remote address %s\n", ip1);
				}
				else {
					bbpagercount++;
				}
				if (p) *p = ':';
				ip1 = strtok(NULL, ",");
			}
			xfree(ips);
		}
		else if (strcmp(argv[opt], "--hobbitd") == 0) {
			usehobbitd = 1;
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
		else if (argnmatch(argv[opt], "--pidfile=")) {
			char *p = strchr(argv[opt], '=');
			pidfile = strdup(p+1);
		}
		else if (argnmatch(argv[opt], "--logfile=")) {
			char *p = strchr(argv[opt], '=');
			logfile = strdup(p+1);
		}
		else if (strcmp(argv[opt], "--log-details") == 0) {
			logdetails = 1;
		}
		else if (argnmatch(argv[opt], "--report=")) {
			char *p1 = strchr(argv[opt], '=')+1;

			if (strchr(p1, '.') == NULL) {
				if (xgetenv("MACHINE") == NULL) {
					errprintf("Environment variable MACHINE is undefined\n");
					return 1;
				}

				proxyname = strdup(xgetenv("MACHINE"));
				proxyname = (char *)realloc(proxyname, strlen(proxyname) + strlen(p1) + 1);
				strcat(proxyname, ".");
				strcat(proxyname, p1);
				proxynamesvc = strdup(p1);
			}
			else {
				proxyname = strdup(p1);
				proxynamesvc = strchr(proxyname, '.')+1;
			}
		}
		else if (strcmp(argv[opt], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[opt], "--version") == 0) {
			printf("bbproxy version %s\n", VERSION);
			return 0;
		}
		else if (strcmp(argv[opt], "--help") == 0) {
			printf("bbproxy version %s\n", VERSION);
			printf("\nOptions:\n");
			printf("\t--listen=IP[:port]          : Listen address and portnumber\n");
			printf("\t--bbdisplay=IP[:port]       : BBDISPLAY server address and portnumber\n");
			printf("\t--bbpager=IP[:port]         : BBPAGER server address and portnumber\n");
			printf("\t--hobbitd                   : Modify behaviour to use hobbitd features\n");
			printf("\t--report=[HOST.]SERVICE     : Sends a BB status message about proxy activity\n");
			printf("\t--timeout=N                 : Communications timeout (seconds)\n");
			printf("\t--lqueue=N                  : Listen-queue size\n");
			printf("\t--daemon                    : Run as a daemon\n");
			printf("\t--no-daemon                 : Do not run as a daemon\n");
			printf("\t--pidfile=FILENAME          : Save proces-ID of daemon to FILENAME\n");
			printf("\t--logfile=FILENAME          : Log to FILENAME instead of stderr\n");
			printf("\t--debug                     : Enable debugging output\n");
			printf("\n");
			return 0;
		}
	}

	if (bbdispcount == 0) {
		errprintf("No BBDISPLAY address given - aborting\n");
		return 1;
	}

	if (bbpagercount == 0) {
		int i;

		for (i = 0; (i < bbdispcount); i++) {
			memcpy(&bbpageraddr[i], &bbdispaddr[i], sizeof(bbpageraddr[i]));
			bbpageraddr[i].sin_port = bbdispaddr[i].sin_port;
			bbpageraddr[i].sin_family = bbdispaddr[i].sin_family;
			bbpagercount++;
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

	errprintf("bbproxy version %s starting\n", VERSION);
	errprintf("Listening on %s:%d\n", inet_ntoa(laddr.sin_addr), ntohs(laddr.sin_port));
	{
		int i;
		char *p;
		char srvrs[500];

		for (i=0, srvrs[0] = '\0', p=srvrs; (i<bbdispcount); i++) {
			p += sprintf(p, "%s:%d ", inet_ntoa(bbdispaddr[i].sin_addr), ntohs(bbdispaddr[i].sin_port));
		}
		errprintf("Sending to BBDISPLAY(s) %s\n", srvrs);

		for (i=0, srvrs[0] = '\0', p=srvrs; (i<bbpagercount); i++) {
			p += sprintf(p, "%s:%d ", inet_ntoa(bbpageraddr[i].sin_addr), ntohs(bbpageraddr[i].sin_port));
		}
		errprintf("Sending to BBPAGER(s) %s\n", srvrs);
	}

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

	setup_signalhandler(proxynamesvc);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigmisc_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);

	do {
		fd_set fdread, fdwrite;
		int maxfd;
		struct timespec tmo;
		struct timeval selecttmo;
		int n, idx;
		conn_t *cwalk, *ctmp;
		time_t ctime;
		time_t now;
		int combining = 0;

		/* See if it is time for a status report */
		if (proxyname && ((now = gettimer()) >= (laststatus+300))) {
			conn_t *stentry;
			int ccount = 0;
			unsigned long bufspace = 0;
			unsigned long avgtime;	/* In millisecs */
			char runtime_s[30];
			unsigned long runt = (unsigned long) (now-startuptime);
			char *p;
			unsigned long msgs_sent = msgs_total - msgs_total_last;

			/* Setup a conn_t struct for the status message */
			stentry = (conn_t *)calloc(1, sizeof(conn_t));
			stentry->state = P_REQ_READY;
			stentry->csocket = stentry->ssocket = -1;
			stentry->clientip = &stentry->caddr.sin_addr;
			getntimer(&stentry->arrival);
			stentry->timelimit.tv_sec = stentry->arrival.tv_sec + timeout;
			stentry->timelimit.tv_nsec = stentry->arrival.tv_nsec;
			stentry->bufsize = BUFSZ_INC;
			stentry->buf = (char *)malloc(stentry->bufsize);
			stentry->next = chead;
			chead = stentry;

			sprintf(runtime_s, "%lu days, %02lu:%02lu:%02lu",
				(runt/86400), ((runt % 86400) / 3600),
				((runt % 3600) / 60), (runt % 60));

			init_timestamp();

			for (cwalk = chead; (cwalk); cwalk = cwalk->next) {
				ccount++;
				bufspace += cwalk->bufsize;
			}

			if (msgs_sent == 0) {
				avgtime = 0;
			}
			else {
				avgtime = (timeinqueue.tv_sec*1000 + timeinqueue.tv_nsec/1000) / msgs_sent;
			}

			p = stentry->buf;
			p += sprintf(p, "combo\nstatus %s green %s Proxy up %s\n\nbbproxy for Hobbit version %s\n\nProxy statistics\n\nIncoming messages        : %10lu (%lu msgs/second)\nOutbound messages        : %10lu\n\nIncoming message distribution\n- Combo messages         : %10lu\n- Status messages        : %10lu\n  Messages merged        : %10lu\n  Resulting combos       : %10lu\n- Page messages          : %10lu\n- Other messages         : %10lu\n\nProxy ressources\n- Connection table size  : %10d\n- Buffer space           : %10lu kByte\n",
				proxyname, timestamp, runtime_s, VERSION,
				msgs_total, (msgs_total - msgs_total_last) / (now - laststatus),
				msgs_delivered,
				msgs_combo, 
				msgs_status, msgs_merged, msgs_combined, 
				msgs_page, msgs_other,
				ccount, bufspace / 1024);
			p += sprintf(p, "\nTimeout/failure details\n");
			p += sprintf(p, "- %-22s : %10lu\n", statename[P_REQ_READING], msgs_timeout_from[P_REQ_READING]);
			p += sprintf(p, "- %-22s : %10lu\n", statename[P_REQ_CONNECTING], msgs_timeout_from[P_REQ_CONNECTING]);
			p += sprintf(p, "- %-22s : %10lu\n", statename[P_REQ_SENDING], msgs_timeout_from[P_REQ_SENDING]);
			p += sprintf(p, "- %-22s : %10lu\n", "recovered", msgs_recovered);
			p += sprintf(p, "- %-22s : %10lu\n", statename[P_RESP_READING], msgs_timeout_from[P_RESP_READING]);
			p += sprintf(p, "- %-22s : %10lu\n", statename[P_RESP_SENDING], msgs_timeout_from[P_RESP_SENDING]);
			p += sprintf(p, "\n%-24s : %10lu.%03lu\n", "Average queue time", 
					(avgtime / 1000), (avgtime % 1000));

			/* Clear the summary collection totals */
			laststatus = now;
			msgs_total_last = msgs_total;
			timeinqueue.tv_sec = timeinqueue.tv_nsec = 0;

			stentry->buflen = strlen(stentry->buf);
			stentry->bufp = stentry->buf + stentry->buflen;
			stentry->state = P_REQ_READY;
		}

		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);
		maxfd = -1;
		combining = 0;

		for (cwalk = chead, idx=0; (cwalk); cwalk = cwalk->next, idx++) {
			dbgprintf("state %d: %s\n", idx, statename[cwalk->state]);

			/* First, handle any state transitions and setup the FD sets for select() */
			switch (cwalk->state) {
			  case P_REQ_READING:
				FD_SET(cwalk->csocket, &fdread); 
				if (cwalk->csocket > maxfd) maxfd = cwalk->csocket; 
				break;

			  case P_REQ_READY:
				if (cwalk->buflen <= 6) {
					/* Got an empty request - just drop it */
					dbgprintf("Dropping empty request from %s\n", inet_ntoa(*cwalk->clientip));
					cwalk->state = P_CLEANUP;
					break;
				}

				if (logdetails) do_log(cwalk);
				cwalk->conntries = CONNECT_TRIES;
				cwalk->sendtries = SEND_TRIES;
				cwalk->conntime = 0;

				/*
				 * We now want to find out what kind of message we've got.
				 * If it's NOT a "status" message, just pass it along.
				 * For "status" messages, we want to try and merge many small
				 * messages into a "combo" message - so send those off the the
				 * P_REQ_COMBINING state for a while.
				 * If we are not going to send back a response to the client, we
				 * also close the client socket since it is no longer needed.
				 * Note that since we started out as optimists and put a "combo\n"
				 * at the front of the buffer, we need to skip that when looking at
				 * what type of message it is. Hence the "cwalk->buf+6".
				 */
				if (strncmp(cwalk->buf+6, "client", 6) == 0) {
					/*
					 * "client" messages go to all bbdisplay servers, but
					 * we will only pass back the response from one of them
					 * (the last one).
					 */
					shutdown(cwalk->csocket, SHUT_RD);
					msgs_other++;
					cwalk->snum = bbdispcount;

					if (usehobbitd && ((cwalk->buflen + 40 ) < cwalk->bufsize)) {
						int n = sprintf(cwalk->bufp, 
								"\n[proxy]\nClientIP:%s\n", 
								inet_ntoa(*cwalk->clientip));
						cwalk->bufp += n;
						cwalk->buflen += n;
					}
				}
				else if ((strncmp(cwalk->buf+6, "query", 5) == 0)  ||
				         (strncmp(cwalk->buf+6, "config", 6) == 0) ||
				         (strncmp(cwalk->buf+6, "ping", 4) == 0) ||
				         (strncmp(cwalk->buf+6, "download", 8) == 0)) {
					/* 
					 * These requests get a response back, but send no data.
					 * Send these to the last of the BBDISPLAY servers only.
					 */
					shutdown(cwalk->csocket, SHUT_RD);
					msgs_other++;
					cwalk->snum = 1;
				}
				else {
					/* It's a request that doesn't take a response. */
					if (cwalk->csocket >= 0) {
						shutdown(cwalk->csocket, SHUT_RDWR);
						close(cwalk->csocket); sockcount--;
						cwalk->csocket = -1;
					}
					cwalk->snum = bbdispcount;

					if (strncmp(cwalk->buf+6, "status", 6) == 0) {
						msgs_status++;
						getntimer(&cwalk->timelimit);
						cwalk->timelimit.tv_nsec += COMBO_DELAY;
						if (cwalk->timelimit.tv_nsec >= 1000000000) {
							cwalk->timelimit.tv_sec++;
							cwalk->timelimit.tv_nsec -= 1000000000;
						}

						/*
						 * Some clients (bbnt) send a trailing \0, so we cannot
						 * rely on buflen being what we want it to be.
						 */
						cwalk->buflen = strlen(cwalk->buf);
						cwalk->bufp = cwalk->buf + cwalk->buflen;

						if (usehobbitd && ((cwalk->buflen + 50 ) < cwalk->bufsize)) {
							int n = sprintf(cwalk->bufp, 
									"\nStatus message received from %s\n", 
									inet_ntoa(*cwalk->clientip));
							cwalk->bufp += n;
							cwalk->buflen += n;
						}

						cwalk->state = P_REQ_COMBINING;
						break;
					}
					else if (strncmp(cwalk->buf+6, "combo\n", 6) == 0) {
						char *currmsg, *nextmsg;

						msgs_combo++;

						/*
						 * Some clients (bbnt) send a trailing \0, so we cannot
						 * rely on buflen being what we want it to be.
						 */
						cwalk->buflen = strlen(cwalk->buf);
						cwalk->bufp = cwalk->buf + cwalk->buflen;

						getntimer(&cwalk->timelimit);
						cwalk->timelimit.tv_nsec += COMBO_DELAY;
						if (cwalk->timelimit.tv_nsec >= 1000000000) {
							cwalk->timelimit.tv_sec++;
							cwalk->timelimit.tv_nsec -= 1000000000;
						}

						currmsg = cwalk->buf+12; /* Skip pre-def. "combo\n" and message "combo\n" */
						do {
							nextmsg = strstr(currmsg, "\n\nstatus");
							if (nextmsg) { *(nextmsg+1) = '\0'; nextmsg += 2; }

							/* Create a duplicate conn_t record for all embedded messages */
							ctmp = (conn_t *)malloc(sizeof(conn_t));
							memcpy(ctmp, cwalk, sizeof(conn_t));
							ctmp->bufsize = BUFSZ_INC*(((6 + strlen(currmsg) + 50) / BUFSZ_INC) + 1);
							ctmp->buf = (char *)malloc(ctmp->bufsize);
							if (usehobbitd) {
								ctmp->buflen = sprintf(ctmp->buf, 
									"combo\n%s\nStatus message received from %s\n", 
									currmsg, inet_ntoa(*cwalk->clientip));
							}
							else {
								ctmp->buflen = sprintf(ctmp->buf, "combo\n%s", currmsg);
							}
							ctmp->bufp = ctmp->buf + ctmp->buflen;
							ctmp->state = P_REQ_COMBINING;
							ctmp->next = chead;
							chead = ctmp;

							currmsg = nextmsg;
						} while (currmsg);

						/* We dont do anymore with this conn_t */
						cwalk->state = P_CLEANUP;
						break;
					}
					else if (strncmp(cwalk->buf+6, "page", 4) == 0) {
						if (usehobbitd) {
							/* hobbitd has no use for page requests */
							cwalk->state = P_CLEANUP;
							break;
						}
						else {
							cwalk->snum = bbpagercount;
							msgs_page++;
						}
					}
					else {
						msgs_other++;
					}
				}

				/* 
				 * This wont be made into a combo message, so skip the "combo\n" 
				 * and go off to send the message to the server.
				 */
				cwalk->bufp = cwalk->buf+6; 
				cwalk->buflen -= 6;
				cwalk->bufpsave = cwalk->bufp;
				cwalk->buflensave = cwalk->buflen;
				cwalk->state = P_REQ_CONNECTING;
				/* Fall through for non-status messages */

			  case P_REQ_CONNECTING:
				/* Need to restore the bufp and buflen, as we may get here many times for one message */
				cwalk->bufp = cwalk->bufpsave;
				cwalk->buflen = cwalk->buflensave;

				ctime = gettimer();
				if (ctime < (cwalk->conntime + CONNECT_INTERVAL)) {
					dbgprintf("Delaying retry of connection\n");
					break;
				}

				cwalk->conntries--;
				cwalk->conntime = ctime;
				if (cwalk->conntries < 0) {
					errprintf("Server not responding, message lost\n");
					cwalk->state = P_REQ_DONE;	/* Not CLENAUP - might be more servers */
					msgs_timeout_from[P_REQ_CONNECTING]++;
					break;
				}

				cwalk->ssocket = socket(AF_INET, SOCK_STREAM, 0);
				if (cwalk->ssocket == -1) {
					dbgprintf("Could not get a socket - will try again\n");
					break; /* Retry the next time around */
				}
				sockcount++;
				fcntl(cwalk->ssocket, F_SETFL, O_NONBLOCK);

				if (strncmp(cwalk->buf, "page", 4) == 0) {
					int idx = (bbpagercount - cwalk->snum);
					n = connect(cwalk->ssocket, (struct sockaddr *)&bbpageraddr[idx], sizeof(bbpageraddr[idx]));
					cwalk->serverip = &bbpageraddr[idx].sin_addr;
					dbgprintf("Connecting to BBPAGER at %s\n", inet_ntoa(*cwalk->serverip));
				}
				else {
					int idx = (bbdispcount - cwalk->snum);
					n = connect(cwalk->ssocket, (struct sockaddr *)&bbdispaddr[idx], sizeof(bbdispaddr[idx]));
					cwalk->serverip = &bbdispaddr[idx].sin_addr;
					dbgprintf("Connecting to BBDISPLAY at %s\n", inet_ntoa(*cwalk->serverip));
				}

				if ((n == 0) || ((n == -1) && (errno == EINPROGRESS))) {
					cwalk->state = P_REQ_SENDING;
					cwalk->connectpending = 1;
					getntimer(&cwalk->timelimit);
					cwalk->timelimit.tv_sec += timeout;
					/* Fallthrough */
				}
				else {
					/* Could not connect! Invoke retries */
					dbgprintf("Connect to server failed: %s\n", strerror(errno));
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
				/* Request has been sent to the server - we're done writing data */
				shutdown(cwalk->ssocket, SHUT_WR);
				cwalk->snum--;
				if (cwalk->snum) {
					/* More servers to do */
					close(cwalk->ssocket); cwalk->ssocket = -1; sockcount--;
					cwalk->conntries = CONNECT_TRIES;
					cwalk->sendtries = SEND_TRIES;
					cwalk->conntime = 0;
					cwalk->state = P_REQ_CONNECTING;
					break;
				}
				else {
					/* Have sent to all servers, grab the response from the last one. */
					cwalk->bufp = cwalk->buf; cwalk->buflen = 0;
					memset(cwalk->buf, 0, cwalk->bufsize);
				}

				msgs_delivered++;

				if (cwalk->sendtries < SEND_TRIES) {
					errprintf("Recovered from write error after %d retries\n", 
						  (SEND_TRIES - cwalk->sendtries));
					msgs_recovered++;
				}

				if (cwalk->arrival.tv_sec > 0) {
					struct timespec departure;

					getntimer(&departure);
					timeinqueue.tv_sec += (departure.tv_sec - cwalk->arrival.tv_sec);
					if (departure.tv_nsec >= cwalk->arrival.tv_nsec) {
						timeinqueue.tv_nsec += (departure.tv_nsec - cwalk->arrival.tv_nsec);
					}
					else {
						timeinqueue.tv_sec--;
						timeinqueue.tv_nsec += (1000000000 + departure.tv_nsec - cwalk->arrival.tv_nsec);
					}

					if (timeinqueue.tv_nsec > 1000000000) {
						timeinqueue.tv_sec++;
						timeinqueue.tv_nsec -= 1000000000;
					}
				}
				else {
					errprintf("Odd ... this message was not timestamped\n");
				}

				if (cwalk->csocket < 0) {
					cwalk->state = P_CLEANUP;
					break;
				}
				else {
					cwalk->state = P_RESP_READING;
					getntimer(&cwalk->timelimit);
					cwalk->timelimit.tv_sec += timeout;
				}
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
				getntimer(&cwalk->timelimit);
				cwalk->timelimit.tv_sec += timeout;
				/* Fall through */

			  case P_RESP_SENDING:
				if (cwalk->buflen && (cwalk->csocket >= 0)) {
					FD_SET(cwalk->csocket, &fdwrite);
					if (cwalk->csocket > maxfd) maxfd = cwalk->csocket;
					break;
				}
				else {
					cwalk->state = P_RESP_DONE;
				}
				/* Fall through */

			  case P_RESP_DONE:
				if (cwalk->csocket >= 0) {
					shutdown(cwalk->csocket, SHUT_WR);
					close(cwalk->csocket); sockcount--;
				}
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
				cwalk->arrival.tv_sec = cwalk->arrival.tv_nsec = 0;
				cwalk->bufp = cwalk->bufp; 
				cwalk->buflen = 0;
				memset(cwalk->buf, 0, cwalk->bufsize);
				memset(&cwalk->caddr, 0, sizeof(cwalk->caddr));
				cwalk->madetocombo = 0;
				cwalk->state = P_IDLE;
				break;

			  case P_IDLE:
				break;

			  case P_REQ_COMBINING:
				/* See if we can combine some "status" messages into a "combo" */
				combining++;
				getntimer(&tmo);
				if ((cwalk->buflen < MINIMUM_FOR_COMBO) && !overdue(&tmo, &cwalk->timelimit)) {
					conn_t *cextra;

					/* Are there any other messages in P_COMBINING state ? */
					cextra = cwalk->next;
					while (cextra && (cextra->state != P_REQ_COMBINING)) cextra = cextra->next;

					if (cextra) {
						/*
						 * Yep. It might be worthwhile to go for a combo.
						 */
						while (cextra && (cwalk->buflen < (MAXIMUM_FOR_COMBO-20))) {
							if (strncmp(cextra->buf+6, "status", 6) == 0) {
								int newsize;

								/*
								 * Size of the new message - if the cextra one
								 * is merged - is the cwalk buffer, plus the
								 * two newlines separating messages in combo's,
								 * plus the cextra buffer except the leading
								 * "combo\n" of 6 bytes.
								 */
								newsize = cwalk->buflen + 2 + (cextra->buflen - 6);

								if ((newsize < cwalk->bufsize) && (newsize < MAXIMUM_FOR_COMBO)) {
									/*
									 * There's room for it. Add it to the
									 * cwalk buffer, but without the leading
									 * "combo\n" (we already have one of those).
									 */
									cwalk->madetocombo++;
									strcat(cwalk->buf, "\n\n");
									strcat(cwalk->buf, cextra->buf+6);
									cwalk->buflen = newsize;
									cextra->state = P_CLEANUP;
									dbgprintf("Merged combo\n");
									msgs_merged++;
								}
							}

							/* Go to the next connection in the right state */
							do {
								cextra = cextra->next;
							} while (cextra && (cextra->state != P_REQ_COMBINING));
						}
					}
				}
				else {
					combining--;
					cwalk->state = P_REQ_CONNECTING;

					if (cwalk->madetocombo) {
						/*
						 * Point the outgoing buffer pointer to the full
						 * message, including the "combo\n"
						 */
						cwalk->bufp = cwalk->buf;
						cwalk->madetocombo++;
						msgs_merged++; /* Count the proginal message also */
						msgs_combined++;
						dbgprintf("Now going to send combo from %d messages\n%s\n", 
							cwalk->madetocombo, cwalk->buf);
					}
					else {
						/*
						 * Skip sending the "combo\n" at start of buffer.
						 */
						cwalk->bufp = cwalk->buf+6;
						cwalk->buflen -= 6;
						dbgprintf("No messages to combine - sending unchanged\n");
					}
				}

				cwalk->bufpsave = cwalk->bufp;
				cwalk->buflensave = cwalk->buflen;
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
			static time_t lastlog = 0;
			if ((now = gettimer()) < (lastlog+30)) {
				lastlog = now;
				errprintf("Squelching incoming connections, sockcount=%d\n", sockcount);
			}
		}

		if (combining) {
			selecttmo.tv_sec = 0; selecttmo.tv_usec = COMBO_DELAY;
		}
		else {
			selecttmo.tv_sec = 1; selecttmo.tv_usec = 0;
		}
		n = select(maxfd+1, &fdread, &fdwrite, NULL, &selecttmo);
		if (n <= 0) {
			getntimer(&tmo);
			for (cwalk = chead; (cwalk); cwalk = cwalk->next) {
				switch (cwalk->state) {
				  case P_REQ_READING:
				  case P_REQ_SENDING:
				  case P_RESP_READING:
				  case P_RESP_SENDING:
					if (overdue(&tmo, &cwalk->timelimit)) {
						cwalk->state = P_CLEANUP;
						msgs_timeout_from[cwalk->state]++;
					}
					break;

				  default:
					break;
				}
			}
		}
		else {
			for (cwalk = chead; (cwalk); cwalk = cwalk->next) {
				switch (cwalk->state) {
				  case P_REQ_READING:
					if (FD_ISSET(cwalk->csocket, &fdread)) {
						do_read(cwalk->csocket, cwalk->clientip, cwalk, P_REQ_READY);
					}
					break;

				  case P_REQ_SENDING:
					if (FD_ISSET(cwalk->ssocket, &fdwrite)) {
						if (cwalk->connectpending) {
							int connres, connressize;

							/* First time ready for write - check connect status */
							cwalk->connectpending = 0;
							connressize = sizeof(connres);
							n = getsockopt(cwalk->ssocket, SOL_SOCKET, SO_ERROR, &connres, &connressize);
							if (connres != 0) {
								/* Connect failed! Invoke retries. */
								dbgprintf("Connect to server failed: %s - retrying\n", 
									strerror(errno));
								close(cwalk->ssocket); sockcount--;
								cwalk->ssocket = -1;
								cwalk->state = P_REQ_CONNECTING;
								break;
							}
						}

						if ( (do_write(cwalk->ssocket, cwalk->serverip, cwalk, P_REQ_DONE) == -1) && 
						     (cwalk->sendtries > 0) ) {
							/*
							 * Got a "write" error after connecting.
							 * Try saving the situation by retrying the send later.
							 */
							dbgprintf("Attempting recovery from write error\n");
							close(cwalk->ssocket); sockcount--; cwalk->ssocket = -1;
							cwalk->sendtries--;
							cwalk->state = P_REQ_CONNECTING;
							cwalk->conntries = CONNECT_TRIES;
							cwalk->conntime = gettimer();
						}
					}
					break;

				  case P_RESP_READING:
					if (FD_ISSET(cwalk->ssocket, &fdread)) {
						do_read(cwalk->ssocket, cwalk->serverip, cwalk, P_RESP_READY);
					}
					break;

				  case P_RESP_SENDING:
					if (FD_ISSET(cwalk->csocket, &fdwrite)) {
						do_write(cwalk->csocket, cwalk->clientip, cwalk, P_RESP_DONE);
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

				dbgprintf("New connection\n");
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

				newconn->connectpending = 0;
				newconn->madetocombo = 0;
				newconn->snum = 0;
				newconn->ssocket = -1;
				newconn->serverip = NULL;
				newconn->conntries = 0;
				newconn->sendtries = 0;
				newconn->timelimit.tv_sec = newconn->timelimit.tv_nsec = 0;

				/*
				 * Why this ? Because we like to merge small status messages
				 * into larger combo messages. So put a "combo\n" at the start 
				 * of the buffer, and then don't send it if we decide it won't
				 * be a combo-message after all.
				 */
				strcpy(newconn->buf, "combo\n");
				newconn->buflen = 6;
				newconn->bufp = newconn->buf+6;

				caddrsize = sizeof(newconn->caddr);
				newconn->csocket = accept(lsocket, (struct sockaddr *)&newconn->caddr, &caddrsize);
				if (newconn->csocket == -1) {
					/* accept() failure. Yes, it does happen! */
					dbgprintf("accept failure, ignoring connection (%s), sockcount=%d\n", 
						strerror(errno), sockcount);
					newconn->state = P_IDLE;
				}
				else {
					msgs_total++;
					newconn->clientip = &newconn->caddr.sin_addr;
					sockcount++;
					fcntl(newconn->csocket, F_SETFL, O_NONBLOCK);
					newconn->state = P_REQ_READING;
					getntimer(&newconn->arrival);
					newconn->timelimit.tv_sec = newconn->arrival.tv_sec + timeout;
					newconn->timelimit.tv_nsec = newconn->arrival.tv_nsec;
				}
			}
		}

		/* Clean up unused conn_t's */
		{
			conn_t *tmp, *khead;

			khead = NULL; cwalk = chead;
			while (cwalk) {
				if ((cwalk == chead) && (cwalk->state == P_IDLE)) {
					/* head of chain is dead */
					tmp = chead;
					chead = chead->next;
					tmp->next = khead;
					khead = tmp;

					cwalk = chead;
				}
				else if (cwalk->next && (cwalk->next->state == P_IDLE)) {
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

			/* khead now holds a list of P_IDLE conn_t structs */
			while (khead) {
				tmp = khead;
				khead = khead->next;

				if (tmp->buf) xfree(tmp->buf);
				xfree(tmp);
			}
		}
	} while (keeprunning);

	if (pidfile) unlink(pidfile);
	return 0;
}

