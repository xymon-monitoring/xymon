/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/smtp-greet-peer.c
 *
 * An SMTP server that records whether the client spoke before being
 * greeted, and answers a pregreeter the way Postfix >= 3.9 does with
 * smtpd_forbid_unauth_pipelining=yes (its default): 554, then hang up.
 *
 * Prints its port on stdout, then one word per line to the verdict file:
 *
 *   polite | PREGREET      -- did anything arrive before our 220?
 *   PIPELINED              -- more than one command in a single write
 *   commands=N
 *
 * Waiting half a second before greeting is what makes the pregreet
 * observable: a client that writes on connect has already done so.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int crlfs(const char *b, int n)
{
	int i, c = 0;
	for (i = 0; i + 1 < n; i++) if ((b[i] == '\r') && (b[i+1] == '\n')) c++;
	return c;
}

int main(int argc, char *argv[])
{
	int sock, c, n, ncmd = 0;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	char buf[2048];
	FILE *out;
	fd_set rfds;
	struct timeval tv;

	if (argc < 2) { fprintf(stderr, "usage: %s VERDICTFILE\n", argv[0]); return 2; }

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { perror("socket"); return 1; }
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
	if (getsockname(sock, (struct sockaddr *)&addr, &alen) < 0) { perror("getsockname"); return 1; }
	if (listen(sock, 4) < 0) { perror("listen"); return 1; }
	printf("%d\n", ntohs(addr.sin_port));
	fflush(stdout);

	tv.tv_sec = 20; tv.tv_usec = 0;
	FD_ZERO(&rfds); FD_SET(sock, &rfds);
	if (select(sock + 1, &rfds, NULL, NULL, &tv) <= 0) return 0;   /* nobody came */
	c = accept(sock, NULL, NULL);
	if (c < 0) return 1;

	out = fopen(argv[1], "w");
	if (!out) { perror("fopen"); return 1; }

	/* Anything readable now arrived BEFORE our greeting. */
	tv.tv_sec = 0; tv.tv_usec = 500000;
	FD_ZERO(&rfds); FD_SET(c, &rfds);
	if (select(c + 1, &rfds, NULL, NULL, &tv) > 0) {
		n = recv(c, buf, sizeof(buf) - 1, 0);
		if (n > 0) {
			buf[n] = '\0';
			fprintf(out, "PREGREET %s\n", buf);
			send(c, "554 5.5.0 Error: SMTP protocol synchronization\r\n", 47, 0);
			fclose(out); close(c); close(sock);
			return 0;
		}
	}
	fprintf(out, "polite\n");
	send(c, "220 test.local ESMTP Postfix\r\n", 30, 0);

	while (ncmd < 2) {
		tv.tv_sec = 8; tv.tv_usec = 0;
		FD_ZERO(&rfds); FD_SET(c, &rfds);
		if (select(c + 1, &rfds, NULL, NULL, &tv) <= 0) break;
		n = recv(c, buf, sizeof(buf) - 1, 0);
		if (n <= 0) break;
		buf[n] = '\0';
		ncmd++;
		if (crlfs(buf, n) > 1) fprintf(out, "PIPELINED %s\n", buf);
		if (strncasecmp(buf, "ehlo", 4) == 0)
			send(c, "250-test.local\r\n250 PIPELINING\r\n", 32, 0);
		else if (strncasecmp(buf, "quit", 4) == 0) {
			send(c, "221 2.0.0 Bye\r\n", 15, 0);
			break;
		}
	}
	fprintf(out, "commands=%d\n", ncmd);
	fclose(out); close(c); close(sock);
	return 0;
}
