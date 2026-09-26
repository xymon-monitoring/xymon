/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/network/protocols-escaped-lengths.c
 *
 * Read every send, expect and until the parser produced, by the LENGTH the
 * parser recorded for it. Built with ASan, so a buffer shorter than its own
 * recorded length is a heap-buffer-overflow here rather than a corrupted
 * write on someone's socket.
 *
 * That is the shape of the bug this exists for: getescapestring() resolves
 * \xNN, so the text may contain a NUL, and anything that copied it with
 * strdup() while carrying the length separately produced exactly this --
 * an allocation that stops at the NUL and a length that does not.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libxymon.h"

/* Touch every byte the length claims, so ASan checks the whole span. */
static unsigned long consume(unsigned char *p, int len)
{
	unsigned long sum = 0;
	int i;

	if (!p || (len <= 0)) return 0;
	for (i = 0; i < len; i++) sum += p[i];
	return sum;
}

int main(int argc, char **argv)
{
	unsigned long sink = 0;
	int i;

	if (argc < 2) { fprintf(stderr, "usage: %s SVCNAME...\n", argv[0]); return 2; }

	init_tcp_services();

	for (i = 1; i < argc; i++) {
		svcinfo_t *svc = find_tcp_service(argv[i]);
		svcstep_t *st;

		if (!svc) { fprintf(stderr, "no such service: %s\n", argv[i]); return 1; }

		sink += consume(svc->sendtxt, svc->sendlen);
		sink += consume(svc->exptext, svc->explen);

		for (st = svc->steps; (st); st = st->next) {
			sink += consume(st->text, st->len);
			sink += consume(st->until, st->untillen);
		}
		printf("%s: ok\n", argv[i]);
	}

	/* Keep the reads from being optimised away. */
	if (sink == 0x5eadbeef) fprintf(stderr, "unreachable\n");
	return 0;
}
