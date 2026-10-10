/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/libxymon/sendmsg-tls-harness.c
 *
 * A one-connection stand-in for xymond's TLS port, used by sendmsg-tls.sh.
 * It listens on an ephemeral port of ADDRESS, prints the port on stdout,
 * takes one connection and reports on stderr what it saw:
 *
 *   tls   -- a TLS server with DIR/server.pem: "handshake: ok | refused",
 *            "message: TEXT", "close_notify: yes | no", and with mtls
 *            "client web01.example.com: 1 | 0". It answers "reply to: TEXT"
 *            and closes with close_notify.
 *   mtls  -- tls, requiring a client certificate issued by DIR/ca.pem.
 *   cut   -- tls, but hangs up after the message without answering or
 *            sending close_notify.
 *   plain -- reads what arrives and prints "first byte: 0xNN": 0x16 opens a
 *            TLS handshake record, a letter a plaintext message.
 *
 * usage: harness DIR ADDRESS tls | mtls | cut | plain [CERT]
 *   CERT names the server's certificate and key, DIR/CERT.pem and
 *   DIR/CERT.key; "server" unless given.
 *
 * Or:    harness send RECIPIENT MESSAGE -- sendmessage() with no reply
 *        buffer, as a program that wants none calls it (the xymon command
 *        always passes one); prints "result: N", N 0 for XYMONSEND_OK.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "config.h"
#include "libxymon.h"
#include "xymontls.h"

int main(int argc, char **argv)
{
	struct addrinfo hints, *ai;
	struct sockaddr_storage addr;
	socklen_t alen = sizeof(addr);
	char port[NI_MAXSERV], cert[1024], key[1024], ca[1024], err[512], msg[4096], reply[4200];
	int lsock, csock, n, len = 0, mtls;
	SSL_CTX *ctx;
	SSL *ssl;

	if ((argc != 4) && (argc != 5)) { fprintf(stderr, "usage: %s DIR ADDRESS tls|mtls|cut|plain [CERT]\n", argv[0]); return 1; }
	if (strcmp(argv[1], "send") == 0) {
		printf("result: %d\n", (int)sendmessage(argv[3], argv[2], 5, NULL));
		return 0;
	}
	mtls = (strcmp(argv[3], "mtls") == 0);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(argv[2], "0", &hints, &ai) != 0) { fprintf(stderr, "%s: not a numeric address\n", argv[2]); return 1; }
	lsock = socket(ai->ai_family, SOCK_STREAM, 0);
	if ((lsock < 0) || (bind(lsock, ai->ai_addr, ai->ai_addrlen) < 0) || (listen(lsock, 1) < 0)) { perror(argv[2]); return 1; }
	freeaddrinfo(ai);
	getsockname(lsock, (struct sockaddr *)&addr, &alen);
	getnameinfo((struct sockaddr *)&addr, alen, NULL, 0, port, sizeof(port), NI_NUMERICSERV);
	printf("%s\n", port);
	fflush(stdout);

	alarm(30);
	csock = accept(lsock, NULL, NULL);
	if (csock < 0) { perror("accept"); return 1; }

	if (strcmp(argv[3], "plain") == 0) {
		n = read(csock, msg, sizeof(msg));
		fprintf(stderr, "first byte: 0x%02x\n", (n > 0) ? (unsigned char)msg[0] : 0);
		close(csock);
		return 0;
	}

	snprintf(cert, sizeof(cert), "%s/%s.pem", argv[1], ((argc == 5) ? argv[4] : "server"));
	snprintf(key, sizeof(key), "%s/%s.key", argv[1], ((argc == 5) ? argv[4] : "server"));
	snprintf(ca, sizeof(ca), "%s/ca.pem", argv[1]);
	ctx = xymontls_server_ctx(cert, key, (mtls ? ca : NULL), mtls, err, sizeof(err));
	if (!ctx) { fprintf(stderr, "context: %s\n", err); return 2; }
	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, csock);
	if (SSL_accept(ssl) != 1) { fprintf(stderr, "handshake: refused\n"); return 0; }
	fprintf(stderr, "handshake: ok\n");
	if (mtls) fprintf(stderr, "client web01.example.com: %d\n", xymontls_peer_has_name(ssl, "web01.example.com"));

	while ((len < (int)sizeof(msg) - 1) && ((n = SSL_read(ssl, msg + len, sizeof(msg) - 1 - len)) > 0)) len += n;
	msg[len] = '\0';
	fprintf(stderr, "message: %s\n", msg);
	fprintf(stderr, "close_notify: %s\n", (SSL_get_shutdown(ssl) & SSL_RECEIVED_SHUTDOWN) ? "yes" : "no");
	if (strcmp(argv[3], "cut") == 0) { close(csock); return 0; }

	n = snprintf(reply, sizeof(reply), "reply to: %s\n", msg);
	SSL_write(ssl, reply, n);
	SSL_shutdown(ssl);
	close(csock);
	return 0;
}
