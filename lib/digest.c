/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement message digest functions (MD5, SHA1 etc.)        */
/*                                                                            */
/* Copyright (C) 2003-2004 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: digest.c,v 1.8 2005-02-21 07:43:22 henrik Exp $";

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
	if (strcmp(digest, "md5") != 0) {
		errprintf("digest_init failure: bbgen was compiled without OpenSSL support\n");
		return NULL;
	}

	/* Use the built in MD5 routines */
	ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
	ctx->digestname = strdup(digest);
	ctx->mdctx = (void *)malloc(sizeof(md5_state_t));
	md5_init((md5_state_t *)ctx->mdctx);
#endif

	return ctx;
}


int digest_data(digestctx_t *ctx, char *buf, int buflen)
{
#ifdef BBGEN_SSL
	EVP_DigestUpdate(ctx->mdctx, buf, buflen);
#else
	md5_append((md5_state_t *)ctx->mdctx, (const md5_byte_t *)buf, buflen);
#endif
	return 0;
}


char *digest_done(digestctx_t *ctx)
{
	char *result = NULL;
	int i, md_len = 0;
	char *p;

#ifdef BBGEN_SSL
	unsigned char md_value[EVP_MAX_MD_SIZE];
	char md_string[2*EVP_MAX_MD_SIZE+128];

	MEMDEFINE(md_string); 
	MEMDEFINE(md_value);

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_DigestFinal_ex(ctx->mdctx, md_value, &md_len);
	EVP_MD_CTX_cleanup(ctx->mdctx);
#else
	EVP_DigestFinal(ctx->mdctx, md_value, &md_len);
	EVP_cleanup();
#endif

	MEMUNDEFINE(md_value);

#else
	/* Built in MD5 hash */
	md5_byte_t md_value[16];
	char md_string[33];

	MEMDEFINE(md_string); 
	md5_finish((md5_state_t *)ctx->mdctx, md_value);
	md_len = sizeof(md_value);
#endif

	sprintf(md_string, "%s:", ctx->digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) {
		p += sprintf(p, "%02x", md_value[i]);
		*p = '\0';
	}
	result = strdup(md_string);

	xfree(ctx->mdctx);
	xfree(ctx);

	MEMUNDEFINE(md_string); 

	return result;
}

