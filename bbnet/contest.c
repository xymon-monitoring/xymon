/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the testing of a TCP service.                    */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: contest.c,v 1.15 2003-05-22 05:56:18 henrik Exp $";

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#if !defined(HPUX)		/* HP-UX has select() and friends in sys/types.h */
#include <sys/select.h>		/* Someday I'll move to GNU Autoconf for this ... */
#endif
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bbtest-net.h"
#include "contest.h"
#include "bbgen.h"
#include "debug.h"
#include "util.h"

#define DEF_MAX_OPENS  (FD_SETSIZE / 4)	/* Max number of simultaneous open connections */
#define MAX_BANNER 1024

static test_t *thead = NULL;

/*
 * Services we know how to handle:
 * This defines what to send to them to shut down the 
 * session nicely, and whether we want to grab the
 * banner or not.
 */
static svcinfo_t svcinfo[] = {
	{ "ftp",     "quit\r\n",          1 },
	{ "ssh",     NULL,                1 },
	{ "ssh1",    NULL,                1 },
	{ "ssh2",    NULL,                1 },
	{ "telnet",  NULL,                0 },
	{ "smtp",    "quit\r\n",          1 },
	{ "pop",     "quit\r\n",          1 },
	{ "pop2",    "quit\r\n",          1 },
	{ "pop-2",   "quit\r\n",          1 },
	{ "pop3",    "quit\r\n",          1 },
	{ "pop-3",   "quit\r\n",          1 },
	{ "imap",    "ABC123 LOGOUT\r\n", 1 },
	{ "imap2",   "ABC123 LOGOUT\r\n", 1 },
	{ "imap3",   "ABC123 LOGOUT\r\n", 1 },
	{ "imap4",   "ABC123 LOGOUT\r\n", 1 },
	{ "nntp",    "quit\r\n",          1 },
	{ "rsync",   NULL,                1 },
	{ NULL,      NULL,                0 }	/* Default behaviour: Dont send anything, dont grab banner */
};


test_t *add_tcp_test(char *ip, int port, char *service, int silent)
{
	test_t *newtest;
	int i;

	if (port == 0) {
		errprintf("Trying to scan port 0 for service %s\n", service);
		errprintf("You probably need to define the %s service in /etc/services\n", service);
		return NULL;
	}

	newtest = (test_t *) malloc(sizeof(test_t));

	memset(&newtest->addr, 0, sizeof(newtest->addr));
	newtest->addr.sin_family = PF_INET;
	newtest->addr.sin_port = htons(port);
	inet_aton(ip, (struct in_addr *) &newtest->addr.sin_addr.s_addr);

	newtest->fd = -1;
	newtest->open = 0;
	newtest->connres = -1;
	newtest->duration.tv_sec = newtest->duration.tv_usec = 0;

	for (i=0; (svcinfo[i].svcname && (strcmp(service, svcinfo[i].svcname) != 0)); i++) ;
	newtest->svcinfo = &svcinfo[i];

	newtest->silenttest = silent;
	newtest->readpending = 0;
	newtest->banner = NULL;
	newtest->next = thead;

	thead = newtest;
	return newtest;
}


void do_tcp_tests(int conntimeout, int concurrency)
{
	int		selres;
	fd_set		readfds, writefds;
	struct timeval	tmo, timestamp;
	struct timezone tz;

	int		activesockets = 0; /* Number of allocated sockets */
	int		pending = 0;	   /* Total number of tests */
	test_t		*nextinqueue;      /* Points to the next item to start testing */
	test_t		*firstactive;      /* Points to the first item currently being tested */
					   /* Thus, active connections are between firstactive..nextinqueue */
	test_t		*item;
	int		sockok;
	int		maxfd;
	int		res, connressize;
	char		msgbuf[MAX_BANNER];


	/* If conntimeout or concurrency are 0, set them to reasonable defaults */
	if (conntimeout == 0) conntimeout = DEF_TIMEOUT;
	if (concurrency == 0) concurrency = DEF_MAX_OPENS;
	if (concurrency > (FD_SETSIZE-10)) {
		concurrency = FD_SETSIZE - 10;	/* Allow a bit for stdin, stdout and such */
		errprintf("bbtest-net: concurrency reduced to FD_SETSIZE-10 (%d)\n", concurrency);
	}

	/* How many tests to do ? */
	for (item = thead; (item); item = item->next) pending++; 
	firstactive = nextinqueue = thead;

	while (pending > 0) {
		/*
		 * First, see if we need to allocate new sockets and initiate connections.
		 */
		for (sockok=1; (sockok && nextinqueue && (activesockets < concurrency)); nextinqueue=nextinqueue->next) {

			/*
			 * We need to allocate a new socket that has O_NONBLOCK set.
			 */
			nextinqueue->fd = socket(PF_INET, SOCK_STREAM, 0);
			sockok = (nextinqueue->fd != -1);
			if (sockok) {
				res = fcntl(nextinqueue->fd, F_SETFL, O_NONBLOCK);
				if (res == 0) {
					/*
					 * Initiate the connection attempt ... 
					 */
					gettimeofday(&nextinqueue->timestart, &tz);
					res = connect(nextinqueue->fd, (struct sockaddr *)&nextinqueue->addr, sizeof(nextinqueue->addr));

					/*
					 * Did it work ?
					 */
					if ((res == 0) || ((res == -1) && (errno == EINPROGRESS))) {
						/* This is OK - EINPROGRES and res=0 pick up status in select() */
						activesockets++;
					}
					else if (res == -1) {
						/* connect() failed. Flag the item as "not open" */
						nextinqueue->connres = errno;
						nextinqueue->open = 0;
						close(nextinqueue->fd);
						nextinqueue->fd = -1;
						pending--;

						switch (nextinqueue->connres) {
						   /* These may happen if connection is refused immediately */
						   case ECONNREFUSED : break;
						   case EHOSTUNREACH : break;
						   case ENETUNREACH  : break;

						   /* Not likely ... */
						   case ETIMEDOUT    : break;

						   /* These should not happen. */
						   case EBADF        : errprintf("connect returned EBADF!\n"); break;
						   case ENOTSOCK     : errprintf("connect returned ENOTSOCK!\n"); break;
						   case EADDRNOTAVAIL: errprintf("connect returned EADDRNOTAVAIL!\n"); break;
						   case EAFNOSUPPORT : errprintf("connect returned EAFNOSUPPORT!\n"); break;
						   case EISCONN      : errprintf("connect returned EISCONN!\n"); break;
						   case EADDRINUSE   : errprintf("connect returned EADDRINUSE!\n"); break;
						   case EFAULT       : errprintf("connect returned EFAULT!\n"); break;
						   case EALREADY     : errprintf("connect returned EALREADY!\n"); break;
						   default           : errprintf("connect returned %d, errno=%d\n", res, errno);
						}
					}
					else {
						/* Should NEVER happen. connect returns 0 or -1 */
						errprintf("Strange result from connect: %d, errno=%d\n", res, errno);
					}
				}
				else {
					/* Could net set to non-blocking mode! Hmmm ... */
					sockok = 0;
					errprintf("Cannot set O_NONBLOCK\n");
				}
			}
			else {
				/* Could not get a socket */
				switch (errno) {
				   case EPROTONOSUPPORT: errprintf("Cannot get socket - EPROTONOSUPPORT\n"); break;
				   case EAFNOSUPPORT   : errprintf("Cannot get socket - EAFNOSUPPORT\n"); break;
				   case EMFILE         : errprintf("Cannot get socket - EMFILE\n"); break;
				   case ENFILE         : errprintf("Cannot get socket - ENFILE\n"); break;
				   case EACCES         : errprintf("Cannot get socket - EACCESS\n"); break;
				   case ENOBUFS        : errprintf("Cannot get socket - ENOBUFS\n"); break;
				   case ENOMEM         : errprintf("Cannot get socket - ENOMEM\n"); break;
				   case EINVAL         : errprintf("Cannot get socket - EINVAL\n"); break;
				   default             : errprintf("Cannot get socket - errno=%d\n", errno); break;
				}
				errprintf("Try running with a lower --concurrency setting (currently: %d)\n", concurrency);
			}
		}

		/* Ready to go - we have a bunch of connections being established */
		dprintf("%d tests pending - %d active tests\n", pending, activesockets);

		/*
		 * Setup the FDSET's
		 */
		FD_ZERO(&readfds); FD_ZERO(&writefds); maxfd = 0;
		for (item=firstactive; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {
				/*
				 * WRITE events are used to signal that a
				 * connection is ready, or it has been refused.
				 * READ events are only interesting for sockets
				 * that have already been found to be open, and
				 * thus have the "readpending" flag set.
				 *
				 * So: On any given socket, we want either a 
				 * write-event or a read-event - never both.
				 */
				if (item->readpending)
					FD_SET(item->fd, &readfds);
				else 
					FD_SET(item->fd, &writefds);

				if (item->fd > maxfd) maxfd = item->fd;
			}
		}

		/*
		 * Wait for something to happen: connect, timeout, banner arrives ...
		 */
		tmo.tv_sec = conntimeout; tmo.tv_usec = 0;
		selres = select((maxfd+1), &readfds, &writefds, NULL, &tmo);
		if (selres == -1) {
			switch (errno) {
			   case EBADF : errprintf("select failed - EBADF\n"); break;
			   case EINTR : errprintf("select failed - EINTR\n"); break;
			   case EINVAL: errprintf("select failed - EINVAL\n"); break;
			   case ENOMEM: errprintf("select failed - ENOMEM\n"); break;
			}
			selres = 0;
		}

		/* Fetch the timestamp so we can tell how long the connect took */
		gettimeofday(&timestamp, &tz);

		/* Now find out which connections had something happen to them */
		for (item=firstactive; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {		/* Only active sockets have this */
				if (selres == 0) {
					/* 
					 * Timeout on all active connection attempts.
					 * Close all sockets.
					 */
					if (item->readpending) {
						/* Final read timeout - just shut this socket */
						shutdown(item->fd, SHUT_RDWR);
					}
					else {
						/* Connection timeout */
						item->open = 0;
						item->connres = ETIMEDOUT;
					}
					close(item->fd);
					item->fd = -1;
					activesockets--;
					pending--;
					if (item == firstactive) firstactive = item->next;
				}
				else {
					if (FD_ISSET(item->fd, &writefds)) {
						/*
						 * Active response on this socket - either OK, or 
						 * connection refused.
						 * We determine what happened by getting the SO_ERROR status.
						 * (cf. select_tut(2) manpage).
						 */
						connressize = sizeof(item->connres);
						res = getsockopt(item->fd, SOL_SOCKET, SO_ERROR, &item->connres, &connressize);
						item->open = (item->connres == 0);

						if (item->open) {
							/*
							 * Connection succeeded - port is open. Determine connection time,
							 * and if we have anything to send then send it.
							 * If we want the banner, set the "readpending" flag to initiate
							 * select() for read()'s.
							 */
							item->duration.tv_sec = timestamp.tv_sec - item->timestart.tv_sec;
							item->duration.tv_usec = timestamp.tv_usec - item->timestart.tv_usec;
							if (item->duration.tv_usec < 0) {
								item->duration.tv_sec--;
								item->duration.tv_usec += 1000000;
							}
							if (item->svcinfo->sendtxt && !item->silenttest) {
								/*
								 * It may be that we cannot write all of the
								 * data we want to. Tough ... 
								 */
								res = write(item->fd, item->svcinfo->sendtxt,
									strlen(item->svcinfo->sendtxt));
							}
							item->readpending = (!item->silenttest && item->svcinfo->grabbanner);
						}

						/* If closed and/or no bannergrabbing, shut down socket */
						if (!item->open || !item->readpending) {
							if (item->open) shutdown(item->fd, SHUT_RDWR);
							close(item->fd);
							item->fd = -1;
							activesockets--;
							pending--;
							if (item == firstactive) firstactive = item->next;
						}
					}
					else if (FD_ISSET(item->fd, &readfds)) {
						/*
						 * Data ready to read on this socket. Grab the
						 * banner - we only do one read (need the socket
						 * for other tests), so if the banner takes more
						 * than one cycle to arrive, too bad!
						 */
						res = read(item->fd, msgbuf, sizeof(msgbuf)-1);
						if (res > 0) {
							msgbuf[res] = '\0';
							item->banner = malloc(res+1);
							strcpy(item->banner, msgbuf);
						}
						shutdown(item->fd, SHUT_RDWR);
						item->readpending = 0;
						close(item->fd);
						item->fd = -1;
						activesockets--;
						pending--;
						if (item == firstactive) firstactive = item->next;
					}
				}
			}
		}  /* end for loop */
	} /* end while (pending) */
}


void show_tcp_test_results(void)
{
	test_t *item;

	for (item = thead; (item); item = item->next) {
		printf("Address=%s:%d, open=%d, res=%d, time=%ld.%06ld, banner='%s'\n", 
				inet_ntoa(item->addr.sin_addr), 
				ntohs(item->addr.sin_port),
				item->open, item->connres, 
				item->duration.tv_sec, item->duration.tv_usec, textornull(item->banner));
	}
}

#ifdef STANDALONE

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

int main(int argc, char *argv[])
{
	add_tcp_test("172.16.10.254", 628, "qmtp", 1);
	add_tcp_test("172.16.10.254", 23, "telnet", 0);
	add_tcp_test("130.228.2.150", 139, "smb", 0);
	add_tcp_test("172.16.10.254", 22, "ssh", 0);
	add_tcp_test("172.16.10.2", 22, "ssh", 0);
	add_tcp_test("172.16.10.1", 22, "ssh", 0);
	add_tcp_test("172.16.10.1", 25, "smtp", 0);
	add_tcp_test("130.228.2.150", 23, "telnet", 0);
	add_tcp_test("130.228.2.150", 21, "ftp", 0);
	add_tcp_test("172.16.10.101", 22, "ssh", 0);

	do_tcp_tests(0, 0);

	return 0;
}
#endif

