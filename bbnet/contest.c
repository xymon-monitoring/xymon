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

#define TIMEOUT 10	/* seconds */
#define MAX_BANNER 1024
#define MAX_OPENS  3	/* Max number of simultaneous open connections */

typedef struct {
	char textaddr[25];
	struct sockaddr_in addr;
	int  tested;
	int  fd;
	int  open;
	int  connres;
	struct timeval timestart, duration;
	void *next;
} test_t;

test_t *thead = NULL;

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
	newtest->connres = -1;
	newtest->duration.tv_sec = newtest->duration.tv_usec = 0;
	newtest->next = thead;
	thead = newtest;
}

void do_conn(void)
{
	int		selres;
	fd_set		writefds;
	struct timeval	tmo, timeend;
	struct timezone tz;

	int		activesockets = 0; /* Number of allocated sockets */
	int		pending = 0;	   /* Total number of tests */
	test_t		*item, *nextinqueue;
	int		sockok;
	int		maxfd;
	int		res, connres, connressize;


	/* How many tests to do ? */
	for (item = thead; (item); item = item->next) pending++; 
	nextinqueue = thead;

	while (pending > 0) {
		/*
		 * First, see if we need to allocate new sockets.
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
					if ((res == -1) && (errno == EINPROGRESS)) {
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

		/*
		 * Setup the FDSET's
		 */
		FD_ZERO(&writefds); maxfd = 0;
		for (item=thead; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {
				FD_SET(item->fd, &writefds);
				if (item->fd > maxfd) maxfd = item->fd;
			}
		}

		/*
		 * Wait for something to happen
		 */
		tmo.tv_sec = TIMEOUT; tmo.tv_usec = 0;
		selres = select((maxfd+1), NULL, &writefds, NULL, &tmo);
		gettimeofday(&timeend, &tz);

		for (item=thead; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {
				if (selres == 0) {
					/* 
					 * Timeout on all active connection attempts.
					 * Close all sockets, and flag them as down.
					 */
					item->open = 0;
					item->connres = ETIMEDOUT;
					close(item->fd);
					item->fd = -1;
					activesockets--;
					pending--;
				}
				else if (FD_ISSET(item->fd, &writefds)) {
					/*
					 * Active response on this socket - either OK, or 
					 * connection refused.
					 */
					connressize = sizeof(connres);
					res = getsockopt(item->fd, SOL_SOCKET, SO_ERROR, &connres, &connressize);
					item->open = (connres == 0);
					item->connres = connres;
					if (item->open) {
						item->duration.tv_sec = timeend.tv_sec - item->timestart.tv_sec;
						item->duration.tv_usec = timeend.tv_usec - item->timestart.tv_usec;
						if (item->duration.tv_usec < 0) {
							item->duration.tv_sec--;
							item->duration.tv_usec += 1000000;
						}
						shutdown(item->fd, SHUT_RDWR);
					}
					close(item->fd);
					item->fd = -1;
					activesockets--;
					pending--;
				}
			}
		}
	}
}

int main(int argc, char *argv[])
{
	test_t *item;

	add_test("172.16.10.254", 25);
	add_test("172.16.10.254", 23);
	add_test("130.228.2.150", 139);
	add_test("172.16.10.254", 22);
	add_test("172.16.10.2", 22);
	add_test("172.16.10.1", 22);
	add_test("172.16.10.1", 25);
	add_test("130.228.2.150", 23);
	add_test("130.228.2.150", 21);
	do_conn();

	for (item = thead; (item); item = item->next) {
		printf("Address=%s, tested=%d, open=%d, res=%d, time=%ld.%ld\n", 
				item->textaddr, 
				item->tested, item->open, item->connres, 
				item->duration.tv_sec, item->duration.tv_usec);
	}

	return 0;
}

