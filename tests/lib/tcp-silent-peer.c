/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/tcp-silent-peer.c
 *
 * Accept connections on a 127.0.0.1 port and then say nothing at all, so a
 * TLS client's handshake never completes. Prints the port on stdout so the
 * test can name it.
 *
 * Holds every connection open rather than closing it: a close would let the
 * client fail fast, and the point is to keep it waiting so the cost of
 * waiting can be measured.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXHELD 64

int main(int argc, char *argv[])
{
	int sock, held[MAXHELD], nheld = 0;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	time_t deadline;
	int seconds = (argc > 1) ? atoi(argv[1]) : 30;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { perror("socket"); return 1; }

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = 0;
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
	if (getsockname(sock, (struct sockaddr *)&addr, &alen) < 0) { perror("getsockname"); return 1; }
	if (listen(sock, 16) < 0) { perror("listen"); return 1; }

	printf("%d\n", ntohs(addr.sin_port));
	fflush(stdout);

	deadline = time(NULL) + seconds;
	while (time(NULL) < deadline) {
		fd_set rfds;
		struct timeval tv;
		int r;

		FD_ZERO(&rfds); FD_SET(sock, &rfds);
		tv.tv_sec = 1; tv.tv_usec = 0;
		r = select(sock + 1, &rfds, NULL, NULL, &tv);
		if ((r > 0) && FD_ISSET(sock, &rfds)) {
			int c = accept(sock, NULL, NULL);
			if (c >= 0) {
				if (nheld < MAXHELD) held[nheld++] = c;
				else close(c);
			}
		}
	}

	while (nheld > 0) close(held[--nheld]);
	close(sock);
	return 0;
}
