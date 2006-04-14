/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is used to implement message digest functions (MD5, SHA1 etc.)        */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: digest.c,v 1.11 2006-04-14 10:19:22 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef BBGEN_SSL
#include <openssl/evp.h>
#endif

#include "libbbgen.h"

digestctx_t *digest_init(char *digest)
{
	struct digestctx_t *ctx = NULL;

#ifdef BBGEN_SSL
	static int dgst_init_done = 0;
	const EVP_MD *md;

	if (!dgst_init_done) {
		OpenSSL_add_all_digests();
		dgst_init_done = 1;
	}

	ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
	ctx->digestname = strdup(digest);
	ctx->digesttype = D_OPENSSL;
	md = EVP_get_digestbyname(ctx->digestname);

	if (!md) {
		xfree(ctx);
		return NULL;
        }

	ctx->mdctx = (void *)malloc(sizeof(EVP_MD_CTX));
#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_MD_CTX_init(ctx->mdctx);
	EVP_DigestInit_ex(ctx->mdctx, md, NULL);
#else
	EVP_DigestInit(ctx->mdctx, md);
#endif

#else
	if (strcmp(digest, "md5") == 0) {
		/* Use the built in MD5 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_MD5;
		ctx->mdctx = (void *)malloc(sizeof(md5_state_t));
		md5_init((md5_state_t *)ctx->mdctx);
	}
	else if (strcmp(digest, "sha1") == 0) {
		/* Use the built in SHA1 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = strdup(digest);
		ctx->digesttype = D_SHA1;
		ctx->mdctx = (void *)malloc(sizeof(mySHA1_CTX));
		mySHA1Init((mySHA1_CTX *)ctx->mdctx);
	}
	else {
		errprintf("digest_init failure: bbgen was compiled without OpenSSL support\n");
		return NULL;
	}
#endif

	return ctx;
}


int digest_data(digestctx_t *ctx, char *buf, int buflen)
{
#ifdef BBGEN_SSL
	EVP_DigestUpdate(ctx->mdctx, buf, buflen);
#else
	switch (ctx->digesttype) {
	  case D_MD5:
		md5_append((md5_state_t *)ctx->mdctx, (const md5_byte_t *)buf, buflen);
		break;
	  case D_SHA1:
		mySHA1Update((mySHA1_CTX *)ctx->mdctx, buf, buflen);
		break;
	  case D_OPENSSL:
		break;
	}
#endif
	return 0;
}


char *digest_done(digestctx_t *ctx)
{
	int i;
	unsigned int md_len = 0;
	char *p;
	unsigned char *md_value;
	char *md_string;

#ifdef BBGEN_SSL
	md_value  = (unsigned char *)malloc(EVP_MAX_MD_SIZE*sizeof(unsigned char));
	md_string = (char *)malloc(2*EVP_MAX_MD_SIZE+128);

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_DigestFinal_ex(ctx->mdctx, md_value, &md_len);
	EVP_MD_CTX_cleanup(ctx->mdctx);
#else
	EVP_DigestFinal(ctx->mdctx, md_value, &md_len);
	EVP_cleanup();
#endif

#else
	switch (ctx->digesttype) {
	  case D_MD5:
		/* Built in MD5 hash */
		md_len = 16;
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		md5_finish((md5_state_t *)ctx->mdctx, md_value);
		break;
	  case D_SHA1:
		/* Built in SHA1 hash */
		md_len = 20;
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		mySHA1Final(md_value, (mySHA1_CTX *)ctx->mdctx);
		break;
	  case D_OPENSSL:
		break;
	}
#endif

	sprintf(md_string, "%s:", ctx->digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) p += sprintf(p, "%02x", md_value[i]);
	*p = '\0';

	xfree(md_value);
	xfree(ctx->mdctx);
	xfree(ctx);

	return md_string;
}

