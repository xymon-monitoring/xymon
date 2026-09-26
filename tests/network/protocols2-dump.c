/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/network/protocols2-dump.c
 *
 * Print the conversation the parser built for each service named on the
 * command line: one line per step, as type and payload. Printing the parsed
 * steps rather than reading the file back is the point -- a step the file
 * declares but the parser drops looks identical in the text and different
 * here, which is exactly the mistake this is watching for.
 *
 * Verdict edges are left out. They are what protocols2.cfg is allowed to add,
 * so the comparison this feeds is about the conversation itself: what is sent,
 * what is awaited, and in which order.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libxymon.h"

static void printesc(unsigned char *s, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if      (s[i] == '\r') printf("\\r");
		else if (s[i] == '\n') printf("\\n");
		else if (s[i] == '\\') printf("\\\\");
		else if ((s[i] < 32) || (s[i] > 126)) printf("\\x%02X", s[i]);
		else putchar(s[i]);
	}
}

int main(int argc, char **argv)
{
	int i;

	if (argc < 2) { fprintf(stderr, "usage: %s SVCNAME...\n", argv[0]); return 2; }

	init_tcp_services();

	for (i = 1; i < argc; i++) {
		svcinfo_t *svc = find_tcp_service(argv[i]);
		svcstep_t *st;

		if (!svc) { fprintf(stderr, "no such service: %s\n", argv[i]); return 1; }

		for (st = svc->steps; st; st = st->next) {
			switch (st->type) {
			  case STEP_SEND:
				printf("%s\tsend\t", argv[i]);
				printesc(st->text, st->len);
				break;

			  case STEP_EXPECT:
				printf("%s\texpect\t", argv[i]);
				printesc(st->text, st->len);
				if (st->until) {
					printf("\tuntil\t");
					printesc(st->until, st->untillen);
				}
				break;

			  case STEP_STARTTLS:	printf("%s\tstarttls\t", argv[i]);	break;
			  case STEP_STARTIAC:	printf("%s\tstartiac\t", argv[i]);	break;

			  case STEP_LABEL:
				/* A state boundary, not a conversation step. */
				continue;

			  default:
				printf("%s\ttype%d\t", argv[i], st->type);
				break;
			}
			putchar('\n');
		}
	}

	return 0;
}
