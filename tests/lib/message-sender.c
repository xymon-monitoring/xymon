/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/message-sender.c
 *
 * Send a message to a TCP port and end the connection in one of two ways:
 *
 *   clean   shutdown(SHUT_WR) -- the peer's read returns end of file, which is
 *           how a Xymon client says its message is complete.
 *   reset   SO_LINGER with a zero timeout, so close() sends RST instead of FIN
 *           and the peer's read FAILS. That is a sender dying mid-message.
 *
 * The difference is the whole point: a protocol delimited by the sender
 * stopping cannot tell the two apart from the bytes alone, only from what the
 * read returned.
 *
 * usage: message-sender HOST PORT clean|reset [TEXT]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	struct linger lg;
	const char *text;
	int sock, reset;
	size_t len;

	if (argc < 4) {
		fprintf(stderr, "usage: %s HOST PORT clean|reset [TEXT]\n", argv[0]);
		return 2;
	}
	reset = (strcmp(argv[3], "reset") == 0);
	text  = (argc > 4) ? argv[4] : "";
	len   = strlen(text);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons((unsigned short)atoi(argv[2]));
	if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
		fprintf(stderr, "bad address: %s\n", argv[1]);
		return 2;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { perror("socket"); return 1; }
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("connect");
		return 1;
	}

	if ((len > 0) && (write(sock, text, len) != (ssize_t)len)) {
		perror("write");
		return 1;
	}

	if (reset) {
		lg.l_onoff = 1; lg.l_linger = 0;
		if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)) != 0) {
			perror("SO_LINGER");
			return 1;
		}
	}
	else if (shutdown(sock, SHUT_WR) != 0) {
		perror("shutdown");
		return 1;
	}

	close(sock);
	return 0;
}
