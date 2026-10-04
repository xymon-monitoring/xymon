/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/xymonnet/contest-tls-harness.c
 *
 * A TCP peer that records the first byte a client sends, for
 * contest-tls.sh. It never sends anything itself; a client opening TLS sends
 * its ClientHello at once -- a handshake record, first byte 0x16 -- where a
 * clear-text client sends its own protocol text.
 *
 * Prints its port on stdout, then one line to the verdict file: the first
 * byte in hex ("16"), or "none" if nothing arrived within 5 seconds.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
	int sock, c;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	unsigned char b;
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

	tv.tv_sec = 5; tv.tv_usec = 0;
	FD_ZERO(&rfds); FD_SET(c, &rfds);
	if ((select(c + 1, &rfds, NULL, NULL, &tv) > 0) && (recv(c, &b, 1, 0) == 1))
		fprintf(out, "%02x\n", b);
	else
		fprintf(out, "none\n");

	fclose(out); close(c); close(sock);
	return 0;
}
