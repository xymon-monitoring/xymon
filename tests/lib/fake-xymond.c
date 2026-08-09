/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/fake-xymond.c
 *
 * Minimal stand-in for xymond in the CGI tests: listen on an ephemeral
 * 127.0.0.1 port (printed on stdout so the test can point XYMONDPORT at
 * it), and answer every connection with the contents of the reply file
 * given as argv[1], after draining the request (the xymon client sends its
 * message, half-closes, then reads until EOF -- lib/sendmsg.c).
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

int main(int argc, char *argv[])
{
	int lsock, csock;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	char buf[4096];
	char *reply;
	long replylen;
	FILE *fd;

	if (argc != 2) { fprintf(stderr, "usage: %s replyfile\n", argv[0]); return 1; }

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

	lsock = socket(AF_INET, SOCK_STREAM, 0);
	if (lsock < 0) { perror("socket"); return 1; }
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = 0;
	if (bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
	if (listen(lsock, 5) < 0) { perror("listen"); return 1; }
	if (getsockname(lsock, (struct sockaddr *)&addr, &alen) < 0) { perror("getsockname"); return 1; }

	printf("%d\n", ntohs(addr.sin_port));
	fflush(stdout);

	for (;;) {
		csock = accept(lsock, NULL, NULL);
		if (csock < 0) continue;
		while (read(csock, buf, sizeof(buf)) > 0) /* drain the request */ ;
		if (write(csock, reply, replylen) != replylen) perror("write");
		close(csock);
	}
}
