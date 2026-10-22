/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/server/xymond-tls-listen-harness.c
 *
 * A TLS client that never ends its message, used by xymond-tls-listen.sh:
 * it connects to ADDRESS:PORT, completes the handshake (no verification),
 * writes MESSAGE, and ends the connection without close_notify -- what a
 * connection cut in mid-message looks like to xymond.
 *
 * usage: harness ADDRESS PORT MESSAGE
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "config.h"
#include "xymontls.h"

int main(int argc, char **argv)
{
	struct addrinfo hints, *ai;
	char err[512];
	SSL_CTX *ctx;
	SSL *ssl;
	int s;

	if (argc != 4) { fprintf(stderr, "usage: %s ADDRESS PORT MESSAGE\n", argv[0]); return 2; }
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(argv[1], argv[2], &hints, &ai) != 0) return 2;
	if (((s = socket(ai->ai_family, SOCK_STREAM, 0)) < 0) || (connect(s, ai->ai_addr, ai->ai_addrlen) != 0)) { perror("connect"); return 1; }

	ctx = xymontls_client_ctx(NULL, NULL, NULL, XYMONTLS_VERIFY_NONE, err, sizeof(err));
	if (!ctx) { fprintf(stderr, "context: %s\n", err); return 1; }
	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, s);
	if (SSL_connect(ssl) != 1) { fprintf(stderr, "handshake failed\n"); return 1; }
	if (SSL_write(ssl, argv[3], strlen(argv[3])) <= 0) { fprintf(stderr, "write failed\n"); return 1; }
	/* No SSL_shutdown: the message is never ended. A half-close, and the
	   rest read off, so the kernel ends the connection with FIN: closing
	   with the server's session tickets unread would reset it instead. */
	shutdown(s, SHUT_WR);
	while (read(s, err, sizeof(err)) > 0) ;
	close(s);
	printf("sent without close_notify\n");
	return 0;
}
