/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/server/xymond-check-tls-harness.c
 *
 * Makes a self-signed certificate for localhost, valid from FROM days to
 * UNTIL days from now (negative: in the past), with its EC P-256 key, for
 * xymond-check-tls.sh: an expired one and one not valid yet, which a
 * checked-in fixture could not stay.
 *
 * usage: harness PREFIX FROM UNTIL   -- writes PREFIX.pem and PREFIX.key
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

int main(int argc, char **argv)
{
	char fn[1024];
	EVP_PKEY *pkey = EVP_PKEY_new();
	EC_KEY *ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	X509 *x = X509_new();
	X509_NAME *name;
	FILE *fd;

	if (argc != 4) { fprintf(stderr, "usage: %s PREFIX FROM UNTIL\n", argv[0]); return 2; }
	if (!ec || !EC_KEY_generate_key(ec) || !EVP_PKEY_assign_EC_KEY(pkey, ec)) { fprintf(stderr, "no key\n"); return 1; }

	X509_set_version(x, 2);
	ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
	X509_gmtime_adj(X509_getm_notBefore(x), (long)atoi(argv[2]) * 86400L);
	X509_gmtime_adj(X509_getm_notAfter(x), (long)atoi(argv[3]) * 86400L);
	X509_set_pubkey(x, pkey);
	name = X509_get_subject_name(x);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"localhost", -1, -1, 0);
	X509_set_issuer_name(x, name);
	if (!X509_sign(x, pkey, EVP_sha256())) { fprintf(stderr, "cannot sign\n"); return 1; }

	snprintf(fn, sizeof(fn), "%s.pem", argv[1]);
	if (!(fd = fopen(fn, "w")) || !PEM_write_X509(fd, x)) { perror(fn); return 1; }
	fclose(fd);
	snprintf(fn, sizeof(fn), "%s.key", argv[1]);
	if (!(fd = fopen(fn, "w")) || !PEM_write_PrivateKey(fd, pkey, NULL, NULL, 0, NULL, NULL)) { perror(fn); return 1; }
	fclose(fd);
	return 0;
}
