/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/server/xymontls-harness.c
 *
 * Driver for lib/xymontls.c, used by xymontls.sh. Runs one TLS handshake over
 * a socket pair -- the client in a child process, the server in this one --
 * with contexts made by the library, and prints what each side saw:
 *
 *   server: ok | refused
 *   client: ok | refused
 *   name NAME: 1 | 0          (xymontls_peer_has_name on the server side)
 *
 * usage: harness DIR CASE
 *   DIR holds ca.pem, server.pem/.key, web01.pem/.key, rogue.pem/.key, the
 *   keys mode 0600. CASE picks the server and client set-up, below.
 * Or:    harness ctx-error CERT KEY  -- the error a server context gives
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "config.h"
#include "xymontls.h"

static char *dir;
static char path[8][1024];
static int npath = 0;

static char *f(char *name)
{
	char *p = path[npath++ % 8];
	snprintf(p, 1024, "%s/%s", dir, name);
	return p;
}

static SSL_CTX *must(SSL_CTX *ctx, char *err)
{
	if (!ctx) { fprintf(stderr, "context: %s\n", err); exit(2); }
	return ctx;
}

int main(int argc, char **argv)
{
	char err[512], *c, *expect = "localhost";
	SSL_CTX *sctx, *cctx;
	SSL *ssl;
	int sv[2], st;
	pid_t pid;
	xymontls_verify_t verify = XYMONTLS_VERIFY_FULL;
	char *names[] = { "web01.example.com", "WEB01.Example.COM", "other.example.com", "not-the-name-checked", NULL };
	int i;

	if ((argc == 4) && (strcmp(argv[1], "ctx-error") == 0)) {
		sctx = xymontls_server_ctx(argv[2], argv[3], NULL, 0, err, sizeof(err));
		printf("%s\n", sctx ? "ok" : err);
		return 0;
	}
	if (argc != 3) { fprintf(stderr, "usage: %s DIR CASE\n", argv[0]); return 2; }
	dir = argv[1]; c = argv[2];

	/* The server: its certificate always; client certificates verified with the CA where a case asks */
	if (strncmp(c, "mtls-", 5) == 0)
		sctx = must(xymontls_server_ctx(f("server.pem"), f("server.key"), f("ca.pem"),
						(strcmp(c, "mtls-optional") != 0), err, sizeof(err)), err);
	else
		sctx = must(xymontls_server_ctx(f("server.pem"), f("server.key"), NULL, 0, err, sizeof(err)), err);

	/* The client */
	if (strcmp(c, "full-wrong-name") == 0) expect = "wrong.example";
	else if (strcmp(c, "full-ipv4") == 0) expect = "127.0.0.1";
	else if (strcmp(c, "full-ipv6") == 0) expect = "::1";
	else if (strcmp(c, "full-other-ip") == 0) expect = "192.0.2.1";
	if (strcmp(c, "none-system-store") == 0) verify = XYMONTLS_VERIFY_NONE;
	if (strcmp(c, "peer-system-store") == 0) verify = XYMONTLS_VERIFY_PEER;

	if ((strcmp(c, "none-system-store") == 0) || (strcmp(c, "peer-system-store") == 0))
		cctx = must(xymontls_client_ctx(NULL, NULL, NULL, verify, err, sizeof(err)), err);
	else if ((strcmp(c, "mtls-web01") == 0) || (strcmp(c, "mtls-optional") == 0))
		cctx = must(xymontls_client_ctx(f("ca.pem"), f("web01.pem"), f("web01.key"), verify, err, sizeof(err)), err);
	else if (strcmp(c, "mtls-rogue") == 0)
		cctx = must(xymontls_client_ctx(f("ca.pem"), f("rogue.pem"), f("rogue.key"), verify, err, sizeof(err)), err);
	else
		cctx = must(xymontls_client_ctx(f("ca.pem"), NULL, NULL, verify, err, sizeof(err)), err);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); return 2; }
	fflush(stdout);
	if ((pid = fork()) == 0) {
		close(sv[0]);
		ssl = SSL_new(cctx);
		SSL_set_fd(ssl, sv[1]);
		if (!xymontls_client_expect(ssl, expect, verify)) _exit(3);
		/* TLS 1.3 reports a refused client certificate on the first read */
		if ((SSL_connect(ssl) == 1) && (SSL_write(ssl, "x", 1) == 1) && (SSL_read(ssl, err, 1) == 1)) _exit(0);
		_exit(1);
	}
	close(sv[1]);
	ssl = SSL_new(sctx);
	SSL_set_fd(ssl, sv[0]);
	if ((SSL_accept(ssl) == 1) && (SSL_read(ssl, err, 1) == 1)) {
		printf("server: ok\n");
		for (i = 0; names[i]; i++) printf("name %s: %d\n", names[i], xymontls_peer_has_name(ssl, names[i]));
		SSL_write(ssl, "y", 1);
	}
	else printf("server: refused\n");
	close(sv[0]);
	waitpid(pid, &st, 0);
	printf("client: %s\n", (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? "ok" : "refused");
	return 0;
}
