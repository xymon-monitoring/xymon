/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/xymonnet/cert-covers-name-harness.c
 *
 * Runs xymonnet's cert_covers_name() -- static in contest.c, so contest.c is
 * included here, built as xymonnetprobe builds it with its main() renamed --
 * on certificates of every kind the sslcert name check meets, and prints each
 * case whose answer is not the documented one: subjectAltName DNS entries,
 * wildcards included, letter case aside, and the subject CN only when there
 * is no DNS subjectAltName (xymonnet(1)), whether or not the certificate is
 * trusted, self-signed or expired. The certificates are made here, so the
 * expired one can be; argv[1] is a CA-signed one from tests/fixtures/tls,
 * which nothing here trusts. Run by tests/xymonnet/cert-covers-name.sh.
 */

#define main xymonnetprobe_main
#include "../../xymonnet/contest.c"
#undef main

#include <openssl/pem.h>

/* Before OpenSSL 1.1, the same accessors had these names */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define X509_getm_notBefore X509_get_notBefore
#define X509_getm_notAfter  X509_get_notAfter
#endif

/* A self-signed certificate: subject CN, subjectAltName (NULL for none), expired or not */
static X509 *make_cert(const char *cn, const char *san, int expired)
{
	EVP_PKEY *key = NULL;
	EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
	X509 *x = X509_new();
	X509_NAME *n = X509_NAME_new();

	EVP_PKEY_keygen_init(kctx);
	EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_X9_62_prime256v1);
	EVP_PKEY_keygen(kctx, &key);
	EVP_PKEY_CTX_free(kctx);

	X509_set_version(x, 2);
	ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
	X509_gmtime_adj(X509_getm_notBefore(x), expired ? -86400L * 60 : -86400L);
	X509_gmtime_adj(X509_getm_notAfter(x), expired ? -86400L * 30 : 86400L * 30);
	X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC, (const unsigned char *)cn, -1, -1, 0);
	X509_set_subject_name(x, n);
	X509_set_issuer_name(x, n);
	X509_NAME_free(n);
	X509_set_pubkey(x, key);
	if (san) {
		X509_EXTENSION *e = X509V3_EXT_conf_nid(NULL, NULL, NID_subject_alt_name, (char *)san);
		X509_add_ext(x, e, -1);
		X509_EXTENSION_free(e);
	}
	X509_sign(x, key, EVP_sha256());
	EVP_PKEY_free(key);
	return x;
}

static int failures = 0;

static void expect(X509 *cert, const char *what, const char *name, int want)
{
	int got = cert_covers_name(cert, name);

	if (got != want) {
		printf("%s, name %s: got %d, want %d\n", what, name, got, want);
		failures++;
	}
}

int main(int argc, char **argv)
{
	X509 *c;
	FILE *f;

	if ((argc < 2) || ((f = fopen(argv[1], "r")) == NULL)) { fprintf(stderr, "usage: %s CA-SIGNED.pem\n", argv[0]); return 2; }
	c = PEM_read_X509(f, NULL, NULL, NULL); fclose(f);
	if (!c) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
	expect(c, "CA-signed, untrusted", "localhost", 1);
	expect(c, "CA-signed, untrusted", "web01.example.com", 0);
	X509_free(c);

	c = make_cert("cn.example.com", "DNS:a.example.com", 0);
	expect(c, "subjectAltName and CN", "a.example.com", 1);
	expect(c, "subjectAltName and CN", "cn.example.com", 0);	/* no CN beside a DNS subjectAltName */
	X509_free(c);

	c = make_cert("cn.example.com", NULL, 0);
	expect(c, "CN only", "cn.example.com", 1);
	expect(c, "CN only", "CN.EXAMPLE.COM", 1);
	expect(c, "CN only", "x.cn.example.com", 0);
	X509_free(c);

	c = make_cert("cn.example.com", "IP:192.0.2.1", 0);
	expect(c, "CN, IP-only subjectAltName", "cn.example.com", 1);
	X509_free(c);

	c = make_cert("*.example.com", NULL, 0);
	expect(c, "wildcard CN only", "www.example.com", 1);
	expect(c, "wildcard CN only", "a.b.example.com", 0);
	X509_free(c);

	c = make_cert("x", "DNS:*.example.com", 0);
	expect(c, "wildcard subjectAltName", "www.example.com", 1);
	expect(c, "wildcard subjectAltName", "WWW.EXAMPLE.COM", 1);
	expect(c, "wildcard subjectAltName", "example.com", 0);
	expect(c, "wildcard subjectAltName", "a.b.example.com", 0);
	X509_free(c);

	c = make_cert("x", "DNS:a.example.com,DNS:b.example.org", 0);
	expect(c, "two subjectAltNames", "b.example.org", 1);
	expect(c, "two subjectAltNames", "c.example.com", 0);
	X509_free(c);

	c = make_cert("x", "DNS:old.example.com", 1);
	expect(c, "expired", "old.example.com", 1);
	expect(c, "expired", "new.example.com", 0);
	X509_free(c);

	printf("%d mismatch(es)\n", failures);
	return failures ? 1 : 0;
}
