/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/network/protocols-driver.c
 *
 * Print, for every service named on the command line, whether the parser
 * routed it to the dialogue driver or left it on the older send-and-match
 * arm. Which of the two runs an entry is invisible from the outside until
 * the one case where they disagree, so it is asserted here directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libxymon.h"

int main(int argc, char **argv)
{
	int i;

	if (argc < 2) { fprintf(stderr, "usage: %s SVCNAME...\n", argv[0]); return 2; }

	init_tcp_services();

	for (i = 1; i < argc; i++) {
		svcinfo_t *svc = find_tcp_service(argv[i]);

		if (!svc) { fprintf(stderr, "no such service: %s\n", argv[i]); return 1; }
		printf("%s: %s\n", argv[i],
		       (svc->flags & TCP_DIALOGUE) ? "driver" : "LEGACY");
	}

	return 0;
}
