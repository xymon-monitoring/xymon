/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/server/xymond-idle-connections-harness.c
 *
 * Network side of xymond-idle-connections.sh, from a chosen source address,
 * IPv4 (to 127.0.0.1) or IPv6 (to ::1):
 *
 *   count SRC PORT N SECONDS -- open N connections that send nothing, 64 at a
 *                               time, wait,
 *                               print "open K" for those xymond has not closed
 *   slow SRC PORT HOST.TEST SECONDS -- start a status, pause SECONDS, finish
 *                               it, print what xymond answered (nothing)
 *   burst SRC PORT N        -- open N connections that each send the start
 *                               of a status at once and hold it, print "open K"
 *                               for those xymond kept, then finish them all
 *
 * Exit status 3 when SRC cannot be bound here, and 4 when a mode has not
 * finished after a minute: every connect() and read() here waits on xymond,
 * and a xymond that stalls must fail the test, saying where, rather than hold
 * it until the CI step's own limit kills it without a word.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * connect(), giving an attempt 5 s: 0 when connected, -1 when refused or
 * failed, -2 when unanswered. The socket is left blocking, as the modes expect.
 */
static int connect_within(int s, struct sockaddr *a, socklen_t len)
{
	struct pollfd p;
	int flags = fcntl(s, F_GETFL), err = 0, rc;
	socklen_t elen = sizeof(err);

	fcntl(s, F_SETFL, flags | O_NONBLOCK);
	rc = connect(s, a, len);
	if ((rc != 0) && (errno == EINPROGRESS)) {
		p.fd = s; p.events = POLLOUT;
		if (poll(&p, 1, 5000) == 0) return -2;
		getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &elen);
		rc = err ? -1 : 0;
	}
	else if (rc != 0) rc = -1;
	fcntl(s, F_SETFL, flags);
	return rc;
}

/*
 * A connection from SRC to xymond on PORT, or -1 when xymond refused it.
 *
 * Each attempt has 5 s, then is made again on a fresh socket, up to three
 * times. On OpenBSD 7.8 with several CPUs, a blocking connect() to xymond,
 * on [::1] or 127.0.0.1, was now and then never answered -- about one run
 * in ten of this test, and on one CPU never: no SYN went out, no half-open
 * connection was listed, and xymond sat idle. A server that only accepts
 * showed it too, more rarely, so it is the kernel's, not xymond's. xymond's
 * own refusals come after accept(), so they are never taken for an
 * unanswered connect().
 */
static int dial(char *src, int port)
{
	struct sockaddr_storage from, to;
	socklen_t len;
	int family = strchr(src, ':') ? AF_INET6 : AF_INET, attempt, rc, s;

	memset(&from, 0, sizeof(from));
	memset(&to, 0, sizeof(to));
	if (family == AF_INET6) {
		struct sockaddr_in6 *f = (struct sockaddr_in6 *)&from, *t = (struct sockaddr_in6 *)&to;

		f->sin6_family = t->sin6_family = AF_INET6;
		inet_pton(AF_INET6, src, &f->sin6_addr);
		inet_pton(AF_INET6, "::1", &t->sin6_addr);
		t->sin6_port = htons(port);
		len = sizeof(struct sockaddr_in6);
	}
	else {
		struct sockaddr_in *f = (struct sockaddr_in *)&from, *t = (struct sockaddr_in *)&to;

		f->sin_family = t->sin_family = AF_INET;
		inet_pton(AF_INET, src, &f->sin_addr);
		inet_pton(AF_INET, "127.0.0.1", &t->sin_addr);
		t->sin_port = htons(port);
		len = sizeof(struct sockaddr_in);
	}
	for (attempt = 0; attempt < 3; attempt++) {
		s = socket(family, SOCK_STREAM, 0);
		if (s < 0) { perror("socket"); exit(3); }
		if (bind(s, (struct sockaddr *)&from, len) != 0) { perror(src); exit(3); }
		rc = connect_within(s, (struct sockaddr *)&to, len);
		if (rc == 0) return s;
		close(s);
		if (rc == -1) return -1;
		fprintf(stderr, "a connect() from %s went unanswered for 5 s; trying again\n", src);
	}
	return -1;
}

static char *mode = "", *source = "";
static volatile int done;

static void too_long(int sig)
{
	char msg[200];
	int len;

	(void)sig;
	len = snprintf(msg, sizeof(msg), "%s from %s: still running after 60 s, %d connection(s) opened\n",
		       mode, source, done);
	if (len > 0) { ssize_t w = write(2, msg, (size_t)len); (void)w; }
	_exit(4);
}

/* 1 while the peer has not closed s */
static int still_open(int s)
{
	char c;
	ssize_t n = recv(s, &c, 1, MSG_DONTWAIT);

	return ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)));
}

int main(int argc, char **argv)
{
	int port, n, i, k = 0, *s;
	char buf[4096];

	if (argc != 6 && argc != 5) { fprintf(stderr, "usage: see the header\n"); return 2; }
	/* A connection xymond refused is closed: writing to it must fail, not end the harness */
	signal(SIGPIPE, SIG_IGN);
	port = atoi(argv[3]);
	mode = argv[1]; source = argv[2];
	signal(SIGALRM, too_long);
	alarm(60);

	if ((strcmp(argv[1], "count") == 0) && (argc == 6)) {
		n = atoi(argv[4]);
		s = calloc(n, sizeof(int));
		for (i = 0; i < n; i++) {
			/*
			 * A pause every 64: OpenBSD's listen queue holds 128, and a
			 * full one drops the next SYN, which the client resends only
			 * after about 6 s -- past xymond's 5 s for a silent
			 * connection, so the first ones would be gone before counting.
			 */
			if (i && ((i % 64) == 0)) usleep(100000);
			s[i] = dial(argv[2], port);
			done = i + 1;
		}
		sleep(atoi(argv[5]));
		for (i = 0; i < n; i++) if ((s[i] >= 0) && still_open(s[i])) k++;
		printf("open %d\n", k);
		return 0;
	}
	if ((strcmp(argv[1], "slow") == 0) && (argc == 6)) {
		int fd = dial(argv[2], port);

		if (fd < 0) { perror("connect"); return 1; }
		done = 1;
		snprintf(buf, sizeof(buf), "status %s green ", argv[4]);
		if (write(fd, buf, strlen(buf)) < 0) { perror("write"); return 1; }
		sleep(atoi(argv[5]));
		snprintf(buf, sizeof(buf), "sent slowly\n");
		if (write(fd, buf, strlen(buf)) < 0) { perror("write after the pause"); return 1; }
		shutdown(fd, SHUT_WR);
		while (read(fd, buf, sizeof(buf)) > 0) ;
		close(fd);
		return 0;
	}
	if ((strcmp(argv[1], "burst") == 0) && (argc == 5)) {
		n = atoi(argv[4]);
		s = calloc(n, sizeof(int));
		for (i = 0; i < n; i++) {
			s[i] = dial(argv[2], port);
			done = i + 1;
			snprintf(buf, sizeof(buf), "status testhost.burst%d green ", i);
			if ((s[i] >= 0) && (write(s[i], buf, strlen(buf)) < 0)) { close(s[i]); s[i] = -1; }
			usleep(5000);	/* lets xymond read it before the next one */
		}
		sleep(1);
		for (i = 0; i < n; i++) if ((s[i] >= 0) && still_open(s[i])) k++;
		printf("open %d\n", k);
		for (i = 0; i < n; i++) {
			if (s[i] < 0) continue;
			if (write(s[i], "in progress\n", 12) == 12) shutdown(s[i], SHUT_WR);
			while (read(s[i], buf, sizeof(buf)) > 0) ;
			close(s[i]);
		}
		return 0;
	}
	fprintf(stderr, "usage: see the header\n");
	return 2;
}
