// SPDX-License-Identifier: GPL-2.0-or-later
//
// tests/client/msgcache-interrupt.c
//
// Interrupted-pull helper for tests/client/msgcache-failed-pull.sh.
//
// Connect to a msgcache, issue "pullclient <id>", half-close the write side so
// it processes the request and starts replying, read just enough to prove the
// batch is being delivered, then abandon the connection with a TCP RST. That
// makes msgcache's remaining write to this socket fail mid-batch -- the case
// where committing "sent" at build time would lose the batch and later let an
// empty retry falsely refresh lastpull.
//
// Standalone: sockets only, no in-tree headers.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv)
{
	int fd, rl;
	struct sockaddr_in sa;
	struct timeval tv;
	struct linger lg;
	char req[64], buf[64];

	if (argc < 3) { fprintf(stderr, "usage: %s PORT IDNUM\n", argv[0]); return 2; }

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) { perror("socket"); return 1; }

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(argv[1]));
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("connect"); return 1; }

	rl = snprintf(req, sizeof req, "pullclient %s\n", argv[2]);
	if ((rl < 0) || (rl >= (int)sizeof req)) { fprintf(stderr, "IDNUM too long\n"); return 2; }
	if (write(fd, req, (size_t)rl) != rl) { perror("write"); return 1; }

	/* msgcache reads a request until EOF before processing it, so half-closing
	   the write side is what makes it build the batch and begin sending. */
	shutdown(fd, SHUT_WR);

	/* Never block forever if the reply does not come. */
	tv.tv_sec = 5; tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

	/* Read a little to prove the batch is being delivered, then stop. */
	(void)read(fd, buf, sizeof buf);

	/* Abandon the rest: SO_LINGER {on, 0} makes close() send a RST, so
	   msgcache's next write to this socket fails rather than quietly buffering
	   the remainder. */
	lg.l_onoff = 1; lg.l_linger = 0;
	setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
	close(fd);
	return 0;
}
