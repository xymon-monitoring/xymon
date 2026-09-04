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
 *
 * With a port argument it does the opposite job: try to bind that port the
 * way xymond would, and report whether the port is actually reserved.
 * Callers use it to check the blocker is blocking before relying on it.
 *
 *   port-blocker          hold an ephemeral port, print it, serve nothing
 *   port-blocker PORT     exit 0 if PORT could be bound (so it is NOT
 *                         reserved), 1 if the bind was refused
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void addr_for(struct sockaddr_in *addr, int port)
{
	memset(addr, 0, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr("127.0.0.1");
	addr->sin_port = htons(port);
}

/* Bind PORT as xymond does, SO_REUSEADDR and all. 0 when the bind succeeded,
   which means nothing was holding the port. */
static int port_is_free(int port)
{
	struct sockaddr_in addr;
	int sock, one = 1, rc;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { perror("socket"); exit(2); }
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	addr_for(&addr, port);
	rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	close(sock);

	return (rc == 0);
}

int main(int argc, char **argv)
{
	int sock;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);

	if (argc > 1) return port_is_free(atoi(argv[1])) ? 0 : 1;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { perror("socket"); return 1; }

	addr_for(&addr, 0);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
	if (getsockname(sock, (struct sockaddr *)&addr, &alen) < 0) { perror("getsockname"); return 1; }

	printf("%d\n", ntohs(addr.sin_port));
	fflush(stdout);

	for (;;) pause();
}
