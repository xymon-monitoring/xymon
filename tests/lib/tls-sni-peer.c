/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/tls-sni-peer.c
 *
 * Accept one TLS connection on a 127.0.0.1 port and report the SNI server
 * name the client put in its ClientHello -- without completing, or even
 * understanding, the handshake. The TLS client speaks first, so reading the
 * ClientHello is enough; the server_name extension is parsed straight from
 * the bytes, so the peer links no TLS library and runs anywhere a C compiler
 * does.
 *
 * Prints two lines to stdout:
 *   <port>\n        as soon as it is listening (so the test can name it)
 *   SNI=<name>\n    after reading the ClientHello ("SNI=" when none was sent)
 *
 * Companion to sni-tcp-tls-behaviour.sh; sibling of tcp-silent-peer.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * Parse the first SNI host_name out of a ClientHello TLS record in buf[0..len).
 * Returns 1 and fills out (NUL-terminated) on success, 0 otherwise. Every step
 * is bounds-checked against len: a short or malformed read yields "no SNI",
 * never a read past the buffer.
 */
static int parse_sni(const unsigned char *buf, size_t len, char *out, size_t outsz)
{
	size_t p, ext_end, clen;

	/* TLS record header: content_type(1)=22 handshake, version(2), length(2) */
	if (len < 5 || buf[0] != 22) return 0;
	p = 5;
	/* Handshake header: msg_type(1)=1 ClientHello, length(3) */
	if (p + 4 > len || buf[p] != 1) return 0;
	p += 4;
	/* client_version(2) + random(32) */
	if (p + 34 > len) return 0;
	p += 34;
	/* session_id: length(1) + bytes */
	if (p + 1 > len) return 0;
	p += 1 + buf[p];
	/* cipher_suites: length(2) + bytes */
	if (p + 2 > len) return 0;
	clen = ((size_t)buf[p] << 8) | buf[p + 1];
	p += 2 + clen;
	/* compression_methods: length(1) + bytes */
	if (p + 1 > len) return 0;
	p += 1 + buf[p];
	/* extensions: total_length(2) + extensions */
	if (p + 2 > len) return 0;
	ext_end = p + 2 + (((size_t)buf[p] << 8) | buf[p + 1]);
	p += 2;
	if (ext_end > len) ext_end = len;

	while (p + 4 <= ext_end) {
		size_t etype = ((size_t)buf[p] << 8) | buf[p + 1];
		size_t elen  = ((size_t)buf[p + 2] << 8) | buf[p + 3];
		size_t e = p + 4;
		if (e + elen > ext_end) break;
		if (etype == 0) {			/* server_name */
			/* server_name_list: list_length(2), then one or more
			 * entries of name_type(1)=0 host_name, name_length(2), name */
			size_t q = e;
			size_t nlen;
			if (q + 2 > e + elen) return 0;
			q += 2;				/* skip list length */
			if (q + 3 > e + elen || buf[q] != 0) return 0;
			nlen = ((size_t)buf[q + 1] << 8) | buf[q + 2];
			q += 3;
			if (q + nlen > e + elen) return 0;
			if (nlen >= outsz) nlen = outsz - 1;
			memcpy(out, buf + q, nlen);
			out[nlen] = '\0';
			return 1;
		}
		p = e + elen;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int sock, c, seconds = (argc > 1) ? atoi(argv[1]) : 10;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	fd_set rfds;
	struct timeval tv;
	unsigned char buf[8192];
	size_t got = 0;
	char name[512];

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { perror("socket"); return 1; }

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = 0;
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
	if (getsockname(sock, (struct sockaddr *)&addr, &alen) < 0) { perror("getsockname"); return 1; }
	if (listen(sock, 8) < 0) { perror("listen"); return 1; }

	printf("%d\n", ntohs(addr.sin_port));
	fflush(stdout);

	/* Bounded wait for the probe to connect, so a run never hangs. */
	FD_ZERO(&rfds); FD_SET(sock, &rfds);
	tv.tv_sec = seconds; tv.tv_usec = 0;
	if (select(sock + 1, &rfds, NULL, NULL, &tv) <= 0) {
		printf("SNI=<timeout>\n"); fflush(stdout); return 2;
	}
	c = accept(sock, NULL, NULL);
	if (c < 0) { perror("accept"); return 1; }

	/* One ClientHello, read until we hold the whole first record or the peer
	 * stops. A ClientHello fits well under the buffer and arrives in a segment
	 * or two; a read timeout keeps a mute client from wedging the test. */
	tv.tv_sec = seconds; tv.tv_usec = 0;
	setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	while (got < sizeof(buf)) {
		ssize_t r = read(c, buf + got, sizeof(buf) - got);
		if (r <= 0) break;
		got += (size_t)r;
		if (got >= 5) {
			size_t reclen = 5 + (((size_t)buf[3] << 8) | buf[4]);
			if (got >= reclen) break;
		}
	}

	if (parse_sni(buf, got, name, sizeof(name)))
		printf("SNI=%s\n", name);
	else
		printf("SNI=\n");
	fflush(stdout);

	close(c);
	close(sock);
	return 0;
}
