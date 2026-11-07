/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* TLS for the Xymon protocol: the contexts xymond and its clients use, and   */
/* the identity a verified certificate carries.                               */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __XYMONTLS_H__
#define __XYMONTLS_H__

#include <stddef.h>

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>

/* How a client checks the server's certificate */
typedef enum {
	XYMONTLS_VERIFY_FULL,	/* the chain, and the name or address connected to */
	XYMONTLS_VERIFY_PEER,	/* the chain only */
	XYMONTLS_VERIFY_NONE	/* nothing: encrypted, not authenticated */
} xymontls_verify_t;

/*
 * Each returns a context, or NULL with the reason in err. TLS 1.2 is the
 * floor. A key file that others may read is refused.
 *
 * Server: certfn and keyfn (keyfn may be NULL: the key is in certfn). With
 * cafn, client certificates chaining to it are verified, and with
 * requirecert a client without one is refused.
 *
 * Client: cafn names the trust store; without it the system's is used.
 * certfn and keyfn, both or neither, give the client a certificate.
 */
extern SSL_CTX *xymontls_server_ctx(char *certfn, char *keyfn, char *cafn, int requirecert, char *err, size_t errsz);
extern SSL_CTX *xymontls_client_ctx(char *cafn, char *certfn, char *keyfn, xymontls_verify_t verify, char *err, size_t errsz);

/*
 * Before a client's handshake: send host as SNI unless it is an address,
 * and with XYMONTLS_VERIFY_FULL require the certificate to name it -- a
 * DNS name, or an IP address. Returns 1, or 0 when host cannot be set.
 */
extern int xymontls_client_expect(SSL *ssl, char *host, xymontls_verify_t verify);

/*
 * After a handshake: 1 when the peer presented a certificate that verified,
 * and one of its subjectAltName DNS entries is name (letter case aside, no
 * wildcards). The subject's CN is not consulted.
 */
extern int xymontls_peer_has_name(SSL *ssl, char *name);

/* 0 when keyfn may be read by others than its owner and group, with the reason in err */
extern int xymontls_key_private(char *keyfn, char *err, size_t errsz);

#endif

#endif
