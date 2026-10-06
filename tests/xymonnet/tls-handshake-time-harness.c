/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/xymonnet/tls-handshake-time-harness.c
 *
 * A TLS server that accepts the TCP connection at once and then waits
 * before answering the handshake, so a client sees a fast connect and a
 * slow handshake. It then sends a one-line banner and closes.
 *
 * The delay is what makes the two measurable apart: a probe that counts
 * the handshake in its response time reports at least the delay, one that
 * stops the clock at the TCP connect reports almost nothing.
 *
 *   argv[1] certificate   argv[2] key   argv[3] delay in milliseconds
 *
 * Prints its port on stdout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

int main(int argc, char *argv[])
{
	int sock, c;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	SSL_CTX *ctx;
	SSL *ssl;
	fd_set rfds;
	struct timeval tv;
	long delayms;

	if (argc < 4) { fprintf(stderr, "usage: %s CERT KEY DELAYMS\n", argv[0]); return 2; }
	delayms = atol(argv[3]);

	SSL_library_init();
	SSL_load_error_strings();
	ctx = SSL_CTX_new(SSLv23_server_method());
	if (!ctx) { fprintf(stderr, "SSL_CTX_new failed\n"); return 1; }
	if (SSL_CTX_use_certificate_file(ctx, argv[1], SSL_FILETYPE_PEM) <= 0 ||
	    SSL_CTX_use_PrivateKey_file(ctx, argv[2], SSL_FILETYPE_PEM) <= 0) {
		fprintf(stderr, "cannot load certificate/key\n");
		return 1;
	}

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

	tv.tv_sec = 25; tv.tv_usec = 0;
	FD_ZERO(&rfds); FD_SET(sock, &rfds);
	if (select(sock + 1, &rfds, NULL, NULL, &tv) <= 0) return 0;
	c = accept(sock, NULL, NULL);
	if (c < 0) return 1;

	/* The TCP connection is up; hold the handshake back. */
	tv.tv_sec = delayms / 1000; tv.tv_usec = (delayms % 1000) * 1000;
	select(0, NULL, NULL, NULL, &tv);

	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, c);
	if (SSL_accept(ssl) > 0) {
		SSL_write(ssl, "OK\r\n", 4);
		SSL_shutdown(ssl);
	}
	SSL_free(ssl);
	close(c); close(sock);
	SSL_CTX_free(ctx);
	return 0;
}
