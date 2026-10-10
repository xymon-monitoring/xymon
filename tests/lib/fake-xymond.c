/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/fake-xymond.c
 *
 * Minimal stand-in for xymond in the CGI tests: listen on an ephemeral
 * port of 127.0.0.1, or of the numeric address given as argv[2] (::1 for
 * IPv6), print the port on stdout so the test can point XYMONDPORT at it,
 * and answer every connection with the contents of the reply file given as
 * argv[1], after draining the request (the xymon client sends its message,
 * half-closes, then reads until EOF -- lib/sendmsg.c).
 *
 * One connection at a time is fine: the CGIs under test run sequentially.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char *argv[])
{
	int lsock, csock;
	struct addrinfo hints, *ai;
	struct sockaddr_storage addr;
	socklen_t alen = sizeof(addr);
	char port[NI_MAXSERV];
	char buf[4096];
	char *reply;
	long replylen;
	FILE *fd;

	if ((argc != 2) && (argc != 3)) { fprintf(stderr, "usage: %s replyfile [address]\n", argv[0]); return 1; }

	fd = fopen(argv[1], "r");
	if (!fd) { perror(argv[1]); return 1; }
	if (fseek(fd, 0, SEEK_END) != 0 || (replylen = ftell(fd)) < 0) {
		fprintf(stderr, "%s: not a seekable regular file\n", argv[1]); return 1;
	}
	rewind(fd);
	reply = malloc(replylen + 1);
	if (!reply) { perror("malloc"); return 1; }
	if (fread(reply, 1, replylen, fd) != (size_t)replylen) { perror("fread"); return 1; }
	fclose(fd);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo((argc == 3) ? argv[2] : "127.0.0.1", "0", &hints, &ai) != 0) {
		fprintf(stderr, "%s: not a numeric address\n", argv[2]); return 1;
	}
	lsock = socket(ai->ai_family, SOCK_STREAM, 0);
	if (lsock < 0) { perror("socket"); return 1; }
	if (bind(lsock, ai->ai_addr, ai->ai_addrlen) < 0) { perror("bind"); return 1; }
	freeaddrinfo(ai);
	if (listen(lsock, 5) < 0) { perror("listen"); return 1; }
	if (getsockname(lsock, (struct sockaddr *)&addr, &alen) < 0) { perror("getsockname"); return 1; }
	if (getnameinfo((struct sockaddr *)&addr, alen, NULL, 0, port, sizeof(port), NI_NUMERICSERV) != 0) {
		fprintf(stderr, "getnameinfo failed\n"); return 1;
	}

	printf("%s\n", port);
	fflush(stdout);

	for (;;) {
		csock = accept(lsock, NULL, NULL);
		if (csock < 0) continue;
		while (read(csock, buf, sizeof(buf)) > 0) /* drain the request */ ;
		if (write(csock, reply, replylen) != replylen) perror("write");
		close(csock);
	}
}
