/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/libxymon/hostname-html-notrunc-harness.c -- for hostname-html-notrunc.sh
 *
 * Loads argv[1] as hosts.cfg and prints hostnamehtml(argv[2], NULL, 0). A host
 * with a very long display name makes the wrapped <A ...><FONT>...</FONT></A>
 * exceed the old fixed 4096-byte buffer; the dynamic buffer must print it whole.
 */
#include <stdio.h>
#include "libxymon.h"

int main(int argc, char *argv[])
{
	if (argc != 3) { fprintf(stderr, "usage: %s hosts.cfg hostname\n", argv[0]); return 2; }
	if (load_hostnames(argv[1], NULL, 1) == -1) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 2;
	}
	fputs(hostnamehtml(argv[2], NULL, 0), stdout);
	return 0;
}
