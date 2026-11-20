/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * cert_dn_string() is static in contest.c, so contest.c is included here with
 * its main() renamed, as cert-covers-name-harness.c does. This builds a
 * certificate subject with an accented, UTF-8 "O=" field and prints what
 * cert_dn_string() makes of it, for the test beside this file to check the
 * result is RFC 2253 and keeps the UTF-8 rather than escaping it as "\xHH".
 */
#define main xymonnetprobe_main
#include "../../xymonnet/contest.c"
#undef main

#include <stdio.h>

int main(void)
{
	X509_NAME *n = X509_NAME_new();
	/* "Societe" with both e's accented, as the UTF-8 bytes C3 A9 */
	static const unsigned char org[] = { 'S','o','c','i', 0xc3,0xa9, 't', 0xc3,0xa9, 0 };
	char *s;

	X509_NAME_add_entry_by_txt(n, "C",  MBSTRING_ASC,  (const unsigned char *)"CH", -1, -1, 0);
	X509_NAME_add_entry_by_txt(n, "O",  MBSTRING_UTF8, org, -1, -1, 0);
	X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC,  (const unsigned char *)"mail.example.com", -1, -1, 0);

	s = cert_dn_string(n);
	printf("%s\n", s);
	xfree(s);
	X509_NAME_free(n);
	return 0;
}
