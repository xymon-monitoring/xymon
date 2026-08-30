/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is used to implement the message digest functions.                    */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DIGEST_H_
#define __DIGEST_H_

typedef enum { D_MD5, D_SHA1, D_SHA256, D_SHA512, D_SHA224, D_SHA384, D_RMD160 } digesttype_t;

typedef struct digestctx_t {
	char *digestname;
	digesttype_t digesttype;
	void *mdctx;
} digestctx_t;

/* The widest digest here is SHA-512. */
#define DIGEST_MAXLEN 64

extern char *md5hash(char *input);
extern digestctx_t *digest_init(char *digest);
extern int digest_data(digestctx_t *ctx, unsigned char *buf, int buflen);
extern char *digest_done(digestctx_t *ctx);
extern int digest_done_raw(digestctx_t *ctx, unsigned char *out, int outsz);
extern int hmac_raw(char *digest, unsigned char *key, int keylen,
		    unsigned char *msg, int msglen, unsigned char *out, int outsz);

#define dohash(P) md5hash(P)

#endif
