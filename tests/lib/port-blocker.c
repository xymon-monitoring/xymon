/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/port-blocker.c
 *
 * Hold a 127.0.0.1 port so that nothing else can bind it, and print the port
 * on stdout so the test can name it.
 *
 * Deliberately does NOT listen(): a connect() to it is refused outright.
 * A listener would be worse than useless here -- the readiness probe in
 * tests/lib/xymond-daemon.sh cannot tell one Xymon-speaking listener from
 * another, so anything that accepts connections reads as "the daemon is up".
 * Refusing the connection is what makes a test of the bind retry deterministic.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(void)
{
	int sock;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { perror("socket"); return 1; }

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = htons(0);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
	if (getsockname(sock, (struct sockaddr *)&addr, &alen) < 0) { perror("getsockname"); return 1; }

	printf("%d\n", ntohs(addr.sin_port));
	fflush(stdout);

	for (;;) pause();
}
