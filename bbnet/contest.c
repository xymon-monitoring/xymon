#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "contest.h"
#include "bbgen.h"
#include "debug.h"

#define DEFTIMEOUT 10	/* seconds */
#define MAX_BANNER 1024
#define MAX_OPENS  (FD_SETSIZE / 2)	/* Max number of simultaneous open connections */

typedef struct {
	char textaddr[25];
	struct sockaddr_in addr;
	int  tested;
	int  fd;
	int  open;
	int  readpending;
	int  connres;
	struct timeval timestart, duration;
	char *banner;
	void *next;
} test_t;

static test_t *thead = NULL;

void add_test(char *ip, int port)
{
	test_t *newtest;

	newtest = (test_t *) malloc(sizeof(test_t));
	sprintf(newtest->textaddr, "%s:%d", ip, port);
	newtest->tested = 0;

	memset(&newtest->addr, 0, sizeof(newtest->addr));
	newtest->addr.sin_port = htons(port);
	newtest->addr.sin_family = PF_INET;
	inet_aton(ip, (struct in_addr *) &newtest->addr.sin_addr.s_addr);

	newtest->fd = -1;
	newtest->open = 0;
	newtest->readpending = 0;
	newtest->connres = -1;
	newtest->duration.tv_sec = newtest->duration.tv_usec = 0;
	newtest->banner = NULL;
	newtest->next = thead;
	thead = newtest;
}

void do_conn(int conntimeout)
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


	/* How many tests to do ? */
	for (item = thead; (item); item = item->next) pending++; 
	firstactive = nextinqueue = thead;

	if (conntimeout == 0) conntimeout = DEFTIMEOUT;


	while (pending > 0) {
		/*
		 * First, see if we need to allocate new sockets and initiate connections.
		 */
		for (sockok=1; (sockok && nextinqueue && (activesockets < MAX_OPENS)); 
			nextinqueue=nextinqueue->next, activesockets++) {

			nextinqueue->fd = socket(PF_INET, SOCK_STREAM, 0);
			sockok = (nextinqueue->fd != -1);
			if (sockok) {
				res = fcntl(nextinqueue->fd, F_SETFL, O_NONBLOCK);
				if (res == 0) {
					gettimeofday(&nextinqueue->timestart, &tz);
					res = connect(nextinqueue->fd, (struct sockaddr *)&nextinqueue->addr, sizeof(nextinqueue->addr));
					if ((res == 0) || ((res == -1) && (errno == EINPROGRESS))) {
						/* This is OK */
					}
					else {
						printf("connect returned %d, errno=%d\n", res, errno);
					}
					nextinqueue->tested = 1;
				}
				else {
					sockok = 0;
					printf("Cannot set O_NONBLOCK\n");
				}
			}
			else {
				printf("Cannot get socket\n");
			}
		}

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
		gettimeofday(&timestamp, &tz);

		for (item=firstactive; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {
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
						 */
						connressize = sizeof(item->connres);
						res = getsockopt(item->fd, SOL_SOCKET, SO_ERROR, &item->connres, &connressize);
						item->open = (item->connres == 0);
						if (item->open) {
							item->duration.tv_sec = timestamp.tv_sec - item->timestart.tv_sec;
							item->duration.tv_usec = timestamp.tv_usec - item->timestart.tv_usec;
							if (item->duration.tv_usec < 0) {
								item->duration.tv_sec--;
								item->duration.tv_usec += 1000000;
							}
							item->readpending = 1;
						}
						else {
							item->readpending = 0;
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

void show_conn_res(void)
{
	test_t *item;

	for (item = thead; (item); item = item->next) {
		printf("Address=%s, tested=%d, open=%d, res=%d, time=%ld.%ld, banner='%s'\n", 
				item->textaddr, 
				item->tested, item->open, item->connres, 
				item->duration.tv_sec, item->duration.tv_usec, textornull(item->banner));
	}
}

#ifdef STANDALONE
int main(int argc, char *argv[])
{

	add_test("172.16.10.254", 628);
	add_test("172.16.10.254", 23);
	add_test("130.228.2.150", 139);
	add_test("172.16.10.254", 22);
	add_test("172.16.10.2", 22);
	add_test("172.16.10.1", 22);
	add_test("172.16.10.1", 25);
	add_test("130.228.2.150", 23);
	add_test("130.228.2.150", 21);
	add_test("172.16.10.101", 22);

	do_conn(0);
	show_conn_res();

	return 0;
}
#endif

