/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/libxymon/anchorsafe-harness.c -- for anchorsafe.sh
 *
 * Prints anchorsafe(argv[1]) with no trailing newline, so the test can compare
 * the exact bytes (a UTF-8 group title must keep its non-ASCII bytes).
 */
#include <stdio.h>
#include "libxymon.h"

int main(int argc, char *argv[])
{
	if (argc < 2) return 2;
	fputs(anchorsafe(argv[1]), stdout);
	return 0;
}
