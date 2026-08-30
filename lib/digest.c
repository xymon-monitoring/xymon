/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is used to implement message digest functions (MD5, SHA1 etc.)        */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libxymon.h"

char *md5hash(char *input)
{
	/* We have a fast MD5 hash function, since that may be used a lot */

	static struct digestctx_t *ctx = NULL;
	unsigned char md_value[16];
	static char md_string[2*16+1];
	int i;
	char *p;

	if (!ctx) {
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup("md5");
		ctx->digesttype = D_MD5;
		ctx->mdctx = (void *)malloc(myMD5_Size());
	}

	myMD5_Init(ctx->mdctx);
	myMD5_Update(ctx->mdctx, input, strlen(input));
	myMD5_Final(md_value, ctx->mdctx);

	for(i = 0, p = md_string; (i < sizeof(md_value)); i++) 
		p += snprintf(p, (sizeof(md_string) - (p - md_string)), "%02x", md_value[i]);
	*p = '\0';

	return md_string;
}


digestctx_t *digest_init(char *digest)
{
	struct digestctx_t *ctx = NULL;

	if (strcmp(digest, "md5") == 0) {
		/* Use the built in MD5 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_MD5;
		ctx->mdctx = (void *)malloc(myMD5_Size());
		myMD5_Init(ctx->mdctx);
	}
	else if (strcmp(digest, "sha1") == 0) {
		/* Use the built in SHA1 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_SHA1;
		ctx->mdctx = (void *)malloc(mySHA1_Size());
		mySHA1_Init(ctx->mdctx);
	}
	else if (strcmp(digest, "rmd160") == 0) {
		/* Use the built in RMD160 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_RMD160;
		ctx->mdctx = (void *)malloc(myRIPEMD160_Size());
		myRIPEMD160_Init(ctx->mdctx);
	}
	else if (strcmp(digest, "sha512") == 0) {
		/* Use the built in SHA-512 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_SHA512;
		ctx->mdctx = (void *)malloc(mySHA512_Size());
		mySHA512_Init(ctx->mdctx);
	}
	else if (strcmp(digest, "sha256") == 0) {
		/* Use the built in SHA-256 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_SHA256;
		ctx->mdctx = (void *)malloc(mySHA256_Size());
		mySHA256_Init(ctx->mdctx);
	}
	else if (strcmp(digest, "sha224") == 0) {
		/* Use the built in SHA-224 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_SHA224;
		ctx->mdctx = (void *)malloc(mySHA224_Size());
		mySHA224_Init(ctx->mdctx);
	}
	else if (strcmp(digest, "sha384") == 0) {
		/* Use the built in SHA-384 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_SHA384;
		ctx->mdctx = (void *)malloc(mySHA384_Size());
		mySHA384_Init(ctx->mdctx);
	}
	else {
		errprintf("digest_init failure: Cannot handle digest %s\n", digest);
		return NULL;
	}

	return ctx;
}


int digest_data(digestctx_t *ctx, unsigned char *buf, int buflen)
{
	switch (ctx->digesttype) {
	  case D_MD5:
		myMD5_Update(ctx->mdctx, buf, buflen);
		break;
	  case D_SHA1:
		mySHA1_Update(ctx->mdctx, buf, buflen);
		break;
	  case D_RMD160:
		myRIPEMD160_Update(ctx->mdctx, buf, buflen);
		break;
	  case D_SHA512:
		mySHA512_Update(ctx->mdctx, buf, buflen);
		break;
	  case D_SHA256:
		mySHA256_Update(ctx->mdctx, buf, buflen);
		break;
	  case D_SHA384:
		mySHA384_Update(ctx->mdctx, buf, buflen);
		break;
	  case D_SHA224:
		mySHA224_Update(ctx->mdctx, buf, buflen);
		break;
	}

	return 0;
}


/* The digest's own output size, in bytes. */
static int digest_len(digesttype_t t)
{
	switch (t) {
	  case D_MD5:    return 16;
	  case D_SHA1:   return 20;
	  case D_RMD160: return 20;
	  case D_SHA224: return 224/8;
	  case D_SHA256: return 256/8;
	  case D_SHA384: return 384/8;
	  case D_SHA512: return 512/8;
	}
	return 0;
}

/*
 * Finish into raw bytes rather than into the "name:hex" digest_done() hands
 * back. HMAC feeds one digest into the next and a dialogue may send a digest
 * over the wire, and neither can use a string with the algorithm's name glued
 * to the front. Frees ctx, as digest_done() does. Returns the length written,
 * or 0 if it does not fit -- out must hold DIGEST_MAXLEN.
 */
int digest_done_raw(digestctx_t *ctx, unsigned char *out, int outsz)
{
	int md_len;

	if (!ctx) return 0;
	md_len = digest_len(ctx->digesttype);
	if ((md_len == 0) || (md_len > outsz)) {
		errprintf("digest_done_raw: %s needs %d bytes, given %d\n",
			  ctx->digestname, md_len, outsz);
		xfree(ctx->digestname); xfree(ctx->mdctx); xfree(ctx);
		return 0;
	}

	switch (ctx->digesttype) {
	  case D_MD5:    myMD5_Final(out, ctx->mdctx); break;
	  case D_SHA1:   mySHA1_Final(out, ctx->mdctx); break;
	  case D_RMD160: myRIPEMD160_Final(out, ctx->mdctx); break;
	  case D_SHA224: mySHA224_Final(out, ctx->mdctx); break;
	  case D_SHA256: mySHA256_Final(out, ctx->mdctx); break;
	  case D_SHA384: mySHA384_Final(out, ctx->mdctx); break;
	  case D_SHA512: mySHA512_Final(out, ctx->mdctx); break;
	}

	xfree(ctx->digestname); xfree(ctx->mdctx); xfree(ctx);
	return md_len;
}

char *digest_done(digestctx_t *ctx)
{
	unsigned char md_value[DIGEST_MAXLEN];
	SBUF_DEFINE(md_string);
	char *name;
	int md_len, i;
	char *p;

	if (!ctx) return NULL;

	/* digest_done_raw() frees ctx, and the name is wanted after it. */
	name = strdup(ctx->digestname);
	md_len = digest_done_raw(ctx, md_value, sizeof(md_value));

	SBUF_MALLOC(md_string, (2*md_len + strlen(name) + 2)*sizeof(char));
	snprintf(md_string, md_string_buflen, "%s:", name);
	for (i = 0, p = md_string + strlen(md_string); (i < md_len); i++)
		p += snprintf(p, (md_string_buflen - (p - md_string)), "%02x", md_value[i]);
	*p = '\0';

	xfree(name);
	return md_string;
}

/*
 * HMAC, RFC 2104, over any digest this file implements. CRAM-MD5, SCRAM and
 * the login exchange of several databases are an HMAC over a shared secret;
 * none of them is expressible with a bare hash, however the hash is nested.
 * Returns the length written, or 0 if the digest is not one we have.
 */
int hmac_raw(char *digest, unsigned char *key, int keylen,
	     unsigned char *msg, int msglen, unsigned char *out, int outsz)
{
	/* RFC 2104 pads the key to the digest's BLOCK size, not its output size. */
	int bs = (((strcmp(digest, "sha384") == 0) || (strcmp(digest, "sha512") == 0)) ? 128 : 64);
	unsigned char k[128], pad[128], inner[DIGEST_MAXLEN];
	int i, innerlen;
	digestctx_t *ctx;

	if ((keylen < 0) || (msglen < 0)) return 0;

	memset(k, 0, sizeof(k));
	if (keylen > bs) {
		/* A key longer than the block is replaced by its own digest. */
		if ((ctx = digest_init(digest)) == NULL) return 0;
		digest_data(ctx, key, keylen);
		if (digest_done_raw(ctx, k, sizeof(k)) == 0) return 0;
	}
	else memcpy(k, key, keylen);

	for (i = 0; (i < bs); i++) pad[i] = k[i] ^ 0x36;
	if ((ctx = digest_init(digest)) == NULL) return 0;
	digest_data(ctx, pad, bs);
	digest_data(ctx, msg, msglen);
	if ((innerlen = digest_done_raw(ctx, inner, sizeof(inner))) == 0) return 0;

	for (i = 0; (i < bs); i++) pad[i] = k[i] ^ 0x5c;
	if ((ctx = digest_init(digest)) == NULL) return 0;
	digest_data(ctx, pad, bs);
	digest_data(ctx, inner, innerlen);
	return digest_done_raw(ctx, out, outsz);
}

