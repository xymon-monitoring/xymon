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

static char rcsid[] = "$Id: digest.c,v 1.5 2005-01-15 17:39:50 henrik Exp $";

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

	ctx = (digestctx_t *) xmalloc(sizeof(digestctx_t));
	ctx->digestname = (char *)xmalloc(strlen(digest)+1);
	strcpy(ctx->digestname, digest);
	md = EVP_get_digestbyname(ctx->digestname);

	if (!md) {
		xfree(ctx);
		return NULL;
        }

	ctx->mdctx = (void *)xmalloc(sizeof(EVP_MD_CTX));
#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_MD_CTX_init(ctx->mdctx);
	EVP_DigestInit_ex(ctx->mdctx, md, NULL);
#else
	EVP_DigestInit(ctx->mdctx, md);
#endif

#else
	errprintf("digest_init failure: bbgen was compiled without OpenSSL support\n");
#endif

	return ctx;
}


int digest_data(digestctx_t *ctx, char *buf, int buflen)
{
#ifdef BBGEN_SSL
	EVP_DigestUpdate(ctx->mdctx, buf, buflen);
#endif
	return 0;
}


char *digest_done(digestctx_t *ctx)
{
	char *result = NULL;

#ifdef BBGEN_SSL
	char md_string[2*EVP_MAX_MD_SIZE+128];
	unsigned char md_value[EVP_MAX_MD_SIZE];
	int md_len, i;
	char *p;

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_DigestFinal_ex(ctx->mdctx, md_value, &md_len);
#else
	EVP_DigestFinal(ctx->mdctx, md_value, &md_len);
#endif

	sprintf(md_string, "%s:", ctx->digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) {
		p += sprintf(p, "%02x", md_value[i]);
		*p = '\0';
	}

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_MD_CTX_cleanup(ctx->mdctx);
#else
	EVP_cleanup();
#endif

	xfree(ctx->mdctx);
	xfree(ctx);

	result = (char *) xmalloc(strlen(md_string)+1);
	strcpy(result, md_string);
#endif

	return result;
}

