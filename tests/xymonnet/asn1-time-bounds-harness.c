/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/xymonnet/asn1-time-bounds-harness.c
 *
 * Calls xymonnet's xymon_ASN1_UTCTIME() -- static in contest.c, so contest.c
 * is included here, built as xymonnetprobe builds it with its main() renamed
 * -- on a certificate time held in exactly as many bytes as it has, with no
 * NUL after it. OpenSSL 4.1 no longer promises that NUL, and its ASan and
 * Valgrind builds report a read of it. Run under AddressSanitizer, a read
 * past the end stops the harness; otherwise it prints the formatted time.
 * Run by tests/xymonnet/asn1-time-bounds.sh.
 */

#define main xymonnetprobe_main
#include "../../xymonnet/contest.c"
#undef main

static void show(const char *text)
{
	ASN1_UTCTIME *t = ASN1_UTCTIME_new();
	size_t len = strlen(text);
	unsigned char *data = malloc(len);

	memcpy(data, text, len);
	ASN1_STRING_set0(t, data, (int)len);	/* t now owns exactly len bytes */
	printf("%s|\n", xymon_ASN1_UTCTIME(t));
	ASN1_UTCTIME_free(t);
}

int main(void)
{
	show("2603151230");		/* YYMMDDHHMM: no seconds, no Z */
	show("2603151230Z");		/* YYMMDDHHMMZ */
	show("260315123045Z");		/* YYMMDDHHMMSSZ */
	show("20260315123045Z");	/* YYYYMMDDHHMMSSZ */
	return 0;
}
