/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/client/msgcache-index-width-harness.c
 *
 * Runs msgcache's own grabdata() on a pull request, with one queued message
 * whose index entry is wider than the old 20-byte buffer, for
 * tests/client/msgcache-index-width.sh.
 *
 * msgcache.c is compiled into this file with its main() renamed, so the
 * harness uses its conn_t, msgqueue_t, qhead and grabdata() as they are.
 *
 * The age in an index entry is now - tstamp, and tstamp is the queued
 * message's own field, so no clock needs faking: a tstamp as far ahead of
 * now as now is from the epoch gives the age a clock reset to the epoch
 * would. With a 1,000,000-byte message the entry is
 * "1000000:-1790000000 " -- 21 bytes with its NUL.
 *
 * Prints the index line grabdata() built, and exits 0 when it is exactly
 * the expected one. Under -fsanitize=address, an overrun of the entry's
 * buffer aborts in grabdata() before that.
 */

#define main msgcache_main
#include "../../client/msgcache.c"
#undef main

#include <sys/socket.h>

int main(void)
{
	int sv[2];
	conn_t conn;
	msgqueue_t msg;
	time_t now;
	char *big, expected[64], *eol;
	size_t biglen = 1000000;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
		perror("socketpair");
		return 2;
	}
	close(sv[1]);	/* The request is already in msgbuf: read() sees EOF. */

	big = malloc(biglen + 1);
	if (big == NULL) return 2;
	memset(big, 'x', biglen);
	big[biglen] = '\0';

	now = getcurrenttime(NULL);
	memset(&msg, 0, sizeof(msg));
	msg.tstamp = now + now;
	msg.msgbuf = newstrbuffer(0);
	addtobuffer(msg.msgbuf, big);
	qhead = qtail = &msg;

	memset(&conn, 0, sizeof(conn));
	conn.sockfd = sv[0];
	conn.action = C_READING;
	conn.msgbuf = newstrbuffer(0);
	addtobuffer(conn.msgbuf, "pullclient\n");

	grabdata(&conn);

	snprintf(expected, sizeof(expected), "%d:%ld \n",
		 (int)biglen, (long)(now - msg.tstamp));
	eol = strchr(STRBUF(conn.msgbuf), '\n');
	if (eol == NULL) {
		fprintf(stderr, "grabdata() built no index line\n");
		return 1;
	}
	printf("%.*s\n", (int)(eol - STRBUF(conn.msgbuf)), STRBUF(conn.msgbuf));
	if (strncmp(STRBUF(conn.msgbuf), expected, strlen(expected)) != 0) {
		fprintf(stderr, "index line is not the expected \"%.*s\"\n",
			(int)strlen(expected) - 1, expected);
		return 1;
	}
	return 0;
}
