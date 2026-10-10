/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* TLS for the Xymon protocol: the contexts xymond and its clients use, and   */
/* the identity a verified certificate carries. See xymontls.h.               */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "xymontls.h"

#ifdef HAVE_OPENSSL

#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

/* The newest OpenSSL error, after prefix, into err */
static void tls_error(char *err, size_t errsz, char *prefix, char *fn)
{
	char reason[256];
	unsigned long e = ERR_get_error();

	if (e) ERR_error_string_n(e, reason, sizeof(reason));
	else snprintf(reason, sizeof(reason), "no OpenSSL error recorded");
	ERR_clear_error();
	snprintf(err, errsz, "%s %s: %s", prefix, (fn ? fn : ""), reason);
}

int xymontls_key_private(char *keyfn, char *err, size_t errsz)
{
	struct stat st;

	if (stat(keyfn, &st) != 0) {
		snprintf(err, errsz, "Cannot read the private key %s", keyfn);
		return 0;
	}
	if (st.st_mode & (S_IROTH | S_IWOTH)) {
		snprintf(err, errsz, "The private key %s may be read by anyone: make it readable by its owner (and group) only", keyfn);
		return 0;
	}
	return 1;
}

static SSL_CTX *tls_new_ctx(int server)
{
	SSL_CTX *ctx;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	ctx = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());
	if (ctx) SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#else
	ctx = SSL_CTX_new(server ? SSLv23_server_method() : SSLv23_client_method());
	if (ctx) SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
#endif
	return ctx;
}

/* The certificate (chain) and its key into ctx; the key may be in certfn */
static int tls_use_cert(SSL_CTX *ctx, char *certfn, char *keyfn, char *err, size_t errsz)
{
	char *kfn = (keyfn ? keyfn : certfn);

	if (!xymontls_key_private(kfn, err, errsz)) return 0;
	if (SSL_CTX_use_certificate_chain_file(ctx, certfn) != 1) {
		tls_error(err, errsz, "Cannot load the certificate", certfn);
		return 0;
	}
	if (SSL_CTX_use_PrivateKey_file(ctx, kfn, SSL_FILETYPE_PEM) != 1) {
		tls_error(err, errsz, "Cannot load the private key", kfn);
		return 0;
	}
	if (SSL_CTX_check_private_key(ctx) != 1) {
		tls_error(err, errsz, "The private key does not match the certificate", certfn);
		return 0;
	}
	return 1;
}

SSL_CTX *xymontls_server_ctx(char *certfn, char *keyfn, char *cafn, int requirecert, char *err, size_t errsz)
{
	SSL_CTX *ctx = tls_new_ctx(1);

	if (!ctx) { tls_error(err, errsz, "Cannot create a TLS context", NULL); return NULL; }
	if (!certfn || !tls_use_cert(ctx, certfn, keyfn, err, errsz)) {
		if (!certfn) snprintf(err, errsz, "A TLS server needs a certificate");
		SSL_CTX_free(ctx);
		return NULL;
	}

	if (cafn) {
		STACK_OF(X509_NAME) *names;

		if (SSL_CTX_load_verify_locations(ctx, cafn, NULL) != 1) {
			tls_error(err, errsz, "Cannot load the client CA", cafn);
			SSL_CTX_free(ctx);
			return NULL;
		}
		names = SSL_load_client_CA_file(cafn);
		if (names) SSL_CTX_set_client_CA_list(ctx, names);
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | (requirecert ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT : 0), NULL);
	}
	else if (requirecert) {
		snprintf(err, errsz, "Requiring client certificates needs a CA to verify them with");
		SSL_CTX_free(ctx);
		return NULL;
	}

	return ctx;
}

SSL_CTX *xymontls_client_ctx(char *cafn, char *certfn, char *keyfn, xymontls_verify_t verify, char *err, size_t errsz)
{
	SSL_CTX *ctx = tls_new_ctx(0);

	if (!ctx) { tls_error(err, errsz, "Cannot create a TLS context", NULL); return NULL; }

	if (verify == XYMONTLS_VERIFY_NONE) {
		SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
	}
	else {
		if (cafn ? (SSL_CTX_load_verify_locations(ctx, cafn, NULL) != 1)
			 : (SSL_CTX_set_default_verify_paths(ctx) != 1)) {
			tls_error(err, errsz, "Cannot load the trusted CA", (cafn ? cafn : "(the system's)"));
			SSL_CTX_free(ctx);
			return NULL;
		}
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	}

	if ((certfn != NULL) != (keyfn != NULL)) {
		snprintf(err, errsz, "A client certificate needs both a certificate and a key file");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if (certfn && !tls_use_cert(ctx, certfn, keyfn, err, errsz)) {
		SSL_CTX_free(ctx);
		return NULL;
	}

	return ctx;
}

int xymontls_client_expect(SSL *ssl, char *host, xymontls_verify_t verify)
{
	unsigned char addr[sizeof(struct in6_addr)];
	int isaddr = (inet_pton(AF_INET, host, addr) == 1) || (inet_pton(AF_INET6, host, addr) == 1);
	X509_VERIFY_PARAM *param = SSL_get0_param(ssl);

	/* RFC 6066: SNI carries a DNS name, never an address */
	if (!isaddr && (SSL_set_tlsext_host_name(ssl, host) != 1)) return 0;

	if (verify != XYMONTLS_VERIFY_FULL) return 1;
	if (isaddr) return (X509_VERIFY_PARAM_set1_ip_asc(param, host) == 1);
	X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
	return (X509_VERIFY_PARAM_set1_host(param, host, 0) == 1);
}

int xymontls_peer_has_name(SSL *ssl, char *name)
{
	X509 *cert;
	GENERAL_NAMES *sans;
	int i, found = 0;
	size_t namelen = strlen(name);

#if (OPENSSL_VERSION_NUMBER >= 0x30000000L) && !defined(LIBRESSL_VERSION_NUMBER)
	cert = SSL_get1_peer_certificate(ssl);
#else
	cert = SSL_get_peer_certificate(ssl);
#endif
	if (!cert) return 0;
	if (SSL_get_verify_result(ssl) != X509_V_OK) { X509_free(cert); return 0; }

	sans = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
	for (i = 0; sans && (i < sk_GENERAL_NAME_num(sans)) && !found; i++) {
		GENERAL_NAME *gn = sk_GENERAL_NAME_value(sans, i);
		const unsigned char *data;
		int len;

		if (gn->type != GEN_DNS) continue;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
		data = ASN1_STRING_get0_data(gn->d.dNSName);
#else
		data = ASN1_STRING_data(gn->d.dNSName);
#endif
		len = ASN1_STRING_length(gn->d.dNSName);
		/* The whole entry: a name with an embedded NUL matches nothing */
		found = (len >= 0) && ((size_t)len == namelen) && (memchr(data, 0, len) == NULL) &&
			(strncasecmp((const char *)data, name, namelen) == 0);
	}
	if (sans) GENERAL_NAMES_free(sans);
	X509_free(cert);

	return found;
}

#else

/* Built without OpenSSL: nothing here (ISO C wants a declaration) */
typedef int xymontls_unused_t;

#endif
