/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/tls-slow-reader.c
 *
 * A TLS server that completes the handshake and then does NOT read for a
 * moment, so a client sending more than the socket buffers hold gets
 * SSL_ERROR_WANT_WRITE part-way through. After the pause it drains
 * everything and reports the byte count.
 *
 * That count is the whole point: a probe that retries the deferred write
 * delivers all of it, one that drops the remainder delivers less, and
 * neither reports an error -- so only the peer can tell them apart.
 *
 *   argv[1] certificate   argv[2] key   argv[3] output file
 *
 * Prints its port on stdout. Writes the byte count, or "ERR ...", to the
 * output file.
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
	FILE *out;
	char buf[65536];
	long total = 0;
	int rcvbuf = 8192;
	fd_set rfds;
	struct timeval tv;

	if (argc < 4) { fprintf(stderr, "usage: %s CERT KEY OUTFILE\n", argv[0]); return 2; }

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
	/* A small receive buffer makes the sender stall sooner and more reliably. */
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
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

	out = fopen(argv[3], "w");
	if (!out) { perror("fopen"); return 1; }

	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, c);
	if (SSL_accept(ssl) <= 0) {
		fprintf(out, "ERR handshake failed\n");
		fclose(out); return 0;
	}

	/* Do NOT read yet: let the sender fill the pipe and hit WANT_WRITE. */
	sleep(2);

	for (;;) {
		int n;

		tv.tv_sec = 8; tv.tv_usec = 0;
		FD_ZERO(&rfds); FD_SET(c, &rfds);
		if (!SSL_pending(ssl) && (select(c + 1, &rfds, NULL, NULL, &tv) <= 0)) break;
		n = SSL_read(ssl, buf, sizeof(buf));
		if (n <= 0) break;
		total += n;
	}

	SSL_write(ssl, "OK\r\n", 4);
	fprintf(out, "%ld\n", total);
	fclose(out);
	SSL_shutdown(ssl);
	SSL_free(ssl);
	close(c); close(sock);
	SSL_CTX_free(ctx);
	return 0;
}
