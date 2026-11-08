/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/lib/dialogue-peer.c
 *
 * A scriptable peer for the protocols.cfg dialogue tests. One program
 * instead of one per case: the exchange is a small script, so a new test
 * is a few lines of text rather than a new C file.
 *
 *   dialogue-peer SCRIPT OBSERVED [CERT KEY]
 *
 * Prints its port on stdout. Appends what it saw to OBSERVED, which is
 * what the test asserts on -- the point being to check what actually
 * went over the wire, not what the source says should have.
 *
 * Script commands, one per line:
 *
 *   send TEXT        write TEXT (\r \n \t \xNN escapes)
 *   recv PREFIX      read one line; record it, and record MISMATCH if it
 *                    does not begin with PREFIX
 *   recvany          read one line and record it
 *   recvraw          read whatever arrives, without waiting for a newline.
 *                    Telnet option refusals are three binary bytes and no
 *                    line ending, so a line-based read waits for a newline
 *                    the client will never send -- which looks exactly like
 *                    the client never answering at all.
 *   replyall TEXT    answer TEXT to every further line, until EOF. Models a
 *                    server that greets and then refuses everything.
 *   starttls         upgrade to TLS here (needs CERT and KEY)
 *   md5check USER SECRET
 *                    read a line, and verify it is "APOP USER <hex>" where
 *                    hex is md5(challenge + SECRET) for the challenge this
 *                    script last sent in angle brackets. The peer computes
 *                    it independently, so a wrong digest fails.
 *   b64check PREFIX TEXT
 *                    read a line and verify it is PREFIX + base64(TEXT).
 *   hold N           stay connected and silent for N seconds
 *   hangup           close the connection
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>

static SSL_CTX *ctx = NULL;
static SSL *ssl = NULL;
static int sock = -1, conn = -1;
static FILE *obs = NULL;
static char challenge[256] = "";

static int io_write(const char *b, int n)
{
	return ssl ? SSL_write(ssl, b, n) : (int)send(conn, b, n, 0);
}

static int io_read(char *b, int n)
{
	return ssl ? SSL_read(ssl, b, n) : (int)recv(conn, b, n, 0);
}

/* One line, up to and including the newline. NULL at EOF. */
/*
 * One read, however few bytes it brings and whatever they are. A line-based
 * read cannot see telnet option refusals: three binary bytes with no line
 * ending, so it waits for a newline that is never coming and the script
 * stalls as though the client had said nothing.
 */
static int readraw(unsigned char *buf, int max)
{
	return io_read((char *)buf, max);
}

static char *readline(char *buf, int max)
{
	int n = 0;

	while (n < max - 1) {
		char c;
		if (io_read(&c, 1) != 1) return (n ? (buf[n] = '\0', buf) : NULL);
		if (c == '\r') continue;
		if (c == '\n') break;
		buf[n++] = c;
	}
	buf[n] = '\0';
	return buf;
}

/*
 * Same quoting rule as protocols.cfg itself: an optional leading quote,
 * and the value ends at the next one. Without this the quotes travel over
 * the wire as part of the payload, and every expect in the test fails for
 * a reason that has nothing to do with the code under test.
 */
static int unescape(const char *in, char *out, int max)
{
	int n = 0, quoted = 0;

	if (*in == '"') { quoted = 1; in++; }

	while (*in && (n < max - 1)) {
		if (quoted && (*in == '"')) break;
		if (*in != '\\') { out[n++] = *in++; continue; }
		in++;
		switch (*in) {
		  case 'r': out[n++] = '\r'; in++; break;
		  case 'n': out[n++] = '\n'; in++; break;
		  case 't': out[n++] = '\t'; in++; break;
		  case 'x': {
			/*
			 * Two digits at most, which is what getescapestring() in
			 * lib/encoding.c does. Reading on while the input is still
			 * hex makes "\x18Connected" 0x8C, eating the C: the peer
			 * would then put bytes on the wire that the protocols.cfg
			 * it stands in for could never produce.
			 */
			int v = 0, d = 0;
			in++;
			while ((d < 2) && isxdigit((int)*in)) {
				v = v * 16 + (isdigit((int)*in) ? *in - '0' : (tolower(*in) - 'a' + 10));
				in++; d++;
			}
			out[n++] = (char)v;
			break;
		  }
		  default: out[n++] = *in++; break;
		}
	}
	out[n] = '\0';
	return n;
}

static void tohex(const unsigned char *d, int n, char *out)
{
	int i;
	for (i = 0; i < n; i++) sprintf(out + 2*i, "%02x", d[i]);
	out[2*n] = '\0';
}

static const char b64set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64(const char *in, char *out)
{
	int i = 0, o = 0, len = strlen(in);

	while (i < len) {
		unsigned long v = (unsigned char)in[i++];
		int pad = 0;
		v <<= 8; if (i < len) v |= (unsigned char)in[i++]; else pad++;
		v <<= 8; if (i < len) v |= (unsigned char)in[i++]; else pad++;
		out[o++] = b64set[(v >> 18) & 63];
		out[o++] = b64set[(v >> 12) & 63];
		out[o++] = (pad > 1) ? '=' : b64set[(v >> 6) & 63];
		out[o++] = (pad > 0) ? '=' : b64set[v & 63];
	}
	out[o] = '\0';
}

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	FILE *sc;
	char line[4096];
	fd_set rfds;
	struct timeval tv;

	if (argc < 3) { fprintf(stderr, "usage: %s SCRIPT OBSERVED [CERT KEY]\n", argv[0]); return 2; }

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
	conn = accept(sock, NULL, NULL);
	if (conn < 0) return 1;

	obs = fopen(argv[2], "w");
	if (!obs) { perror("fopen"); return 1; }
	/*
	 * Line buffered: a test reads this file while the peer is still running,
	 * and the default block buffering holds everything until fclose(). A peer
	 * sitting in a "hold" then shows an empty observation log, so an assertion
	 * about what it received fails on timing rather than on behaviour.
	 */
	setvbuf(obs, NULL, _IOLBF, 0);

	sc = fopen(argv[1], "r");
	if (!sc) { fprintf(obs, "ERR no script\n"); fclose(obs); return 1; }

	while (fgets(line, sizeof(line), sc)) {
		char *cmd, *arg, buf[4096], got[4096];
		int n;

		line[strcspn(line, "\r\n")] = '\0';
		cmd = line + strspn(line, " \t");
		if ((*cmd == '\0') || (*cmd == '#')) continue;
		arg = strchr(cmd, ' ');
		if (arg) *arg++ = '\0'; else arg = "";

		if (strcmp(cmd, "send") == 0) {
			char *p;

			n = unescape(arg, buf, sizeof(buf));
			io_write(buf, n);
			/* Remember an APOP-style challenge for md5check. */
			p = strchr(buf, '<');
			if (p) {
				char *e = strchr(p, '>');
				if (e && ((e - p + 1) < (int)sizeof(challenge))) {
					memcpy(challenge, p, e - p + 1);
					challenge[e - p + 1] = '\0';
				}
			}
		}
		else if (strcmp(cmd, "recvraw") == 0) {
			unsigned char raw[512];
			int got_n = readraw(raw, sizeof(raw));

			if (got_n <= 0) { fprintf(obs, "eof\n"); break; }
			fprintf(obs, "gotraw %d bytes\n", got_n);
		}
		else if ((strcmp(cmd, "recv") == 0) || (strcmp(cmd, "recvany") == 0)) {
			if (!readline(got, sizeof(got))) { fprintf(obs, "eof\n"); break; }
			fprintf(obs, "got %s\n", got);
			if (*arg && strncmp(got, arg, strlen(arg)) != 0)
				fprintf(obs, "MISMATCH want=%s got=%s\n", arg, got);
		}
		else if (strcmp(cmd, "replyall") == 0) {
			n = unescape(arg, buf, sizeof(buf));
			while (readline(got, sizeof(got))) {
				fprintf(obs, "got %s\n", got);
				io_write(buf, n);
			}
			fprintf(obs, "eof\n");
			break;
		}
		else if (strcmp(cmd, "starttls") == 0) {
			if (argc < 5) { fprintf(obs, "ERR no certificate\n"); break; }
			SSL_library_init();
			ctx = SSL_CTX_new(SSLv23_server_method());
			if (!ctx ||
			    (SSL_CTX_use_certificate_file(ctx, argv[3], SSL_FILETYPE_PEM) <= 0) ||
			    (SSL_CTX_use_PrivateKey_file(ctx, argv[4], SSL_FILETYPE_PEM) <= 0)) {
				fprintf(obs, "ERR bad certificate\n");
				break;
			}
			ssl = SSL_new(ctx);
			SSL_set_fd(ssl, conn);
			if (SSL_accept(ssl) <= 0) { fprintf(obs, "ERR handshake failed\n"); ssl = NULL; break; }
			fprintf(obs, "tls-ok\n");
		}
		else if (strcmp(cmd, "md5check") == 0) {
			char user[128] = "", secret[128] = "", want[1024], hex[33], src[512];
			unsigned char dig[EVP_MAX_MD_SIZE];
			unsigned int diglen = 0;

			sscanf(arg, "%127s %127s", user, secret);
			if (!readline(got, sizeof(got))) { fprintf(obs, "eof\n"); break; }
			snprintf(src, sizeof(src), "%s%s", challenge, secret);
			EVP_Digest(src, strlen(src), dig, &diglen, EVP_md5(), NULL);
			tohex(dig, (int)diglen, hex);
			snprintf(want, sizeof(want), "APOP %s %s", user, hex);
			fprintf(obs, "got %s\n", got);
			if (strcmp(got, want) != 0) fprintf(obs, "MISMATCH want=%s got=%s\n", want, got);
			else fprintf(obs, "md5-ok\n");
		}
		else if (strcmp(cmd, "b64check") == 0) {
			char pfx[128] = "", txt[256] = "", want[1024], enc[512];

			sscanf(arg, "%127s %255s", pfx, txt);
			if (!readline(got, sizeof(got))) { fprintf(obs, "eof\n"); break; }
			b64(txt, enc);
			snprintf(want, sizeof(want), "%s%s", pfx, enc);
			fprintf(obs, "got %s\n", got);
			if (strcmp(got, want) != 0) fprintf(obs, "MISMATCH want=%s got=%s\n", want, got);
			else fprintf(obs, "b64-ok\n");
		}
		else if (strcmp(cmd, "hold") == 0) {
			/*
			 * Stay connected and silent. A script ends by closing the
			 * socket, which is itself an event the probe reacts to -- so
			 * a test that wants to watch a timeout, or watch the probe
			 * give up on its own, has to keep the connection open and say
			 * nothing. Sleeping is exactly that.
			 */
			int secs = atoi(arg);

			if (secs > 0) sleep(secs);
		}
		else if (strcmp(cmd, "hangup") == 0) break;
		else {
			/*
			 * Say so. "hold" was used by four scripts before it existed
			 * here, and was skipped in silence: the peer closed the moment
			 * the script ran out, while the file read as though it waited.
			 * A script that asks for something this peer cannot do is a
			 * test that is not doing what it says.
			 */
			fprintf(obs, "UNKNOWN %s\n", cmd);
			fprintf(stderr, "dialogue-peer: unknown command: %s\n", cmd);
		}
	}

	fprintf(obs, "done\n");
	fclose(obs); fclose(sc);
	if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
	if (ctx) SSL_CTX_free(ctx);
	if (conn >= 0) close(conn);
	close(sock);
	return 0;
}
