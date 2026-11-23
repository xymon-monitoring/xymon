/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/libxymon/rfc2047-decode-harness.c -- for rfc2047-decode.sh
 *
 * Prints rfc2047_decode(argv[1]) with no trailing newline, so the test can
 * compare the exact decoded bytes. Built under AddressSanitizer when it is
 * available, so a high byte fed into a B-word's base64 (which indexes
 * base64decode's table) faults if the table is too narrow.
 */
#include <stdio.h>
#include "../../lib/encoding.c"

int main(int argc, char *argv[])
{
	char *out;
	if (argc != 2) return 2;
	out = rfc2047_decode(argv[1]);
	fputs(out, stdout);
	xfree(out);
	return 0;
}
