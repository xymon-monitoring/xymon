/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement message digest functions (MD5, SHA1 etc.)        */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: digest.c,v 1.2 2004-07-19 15:41:14 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "digest.h"

digestctx_t *digest_init(char *digest)
{
	static int dgst_init_done = 0;
	const EVP_MD *md;
	struct digestctx_t *ctx;

	if (!dgst_init_done) {
		OpenSSL_add_all_digests();
		dgst_init_done = 1;
	}

	ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
	ctx->digestname = (char *)malloc(strlen(digest)+1);
	strcpy(ctx->digestname, digest);
	md = EVP_get_digestbyname(ctx->digestname);

	if (!md) {
		free(ctx);
		return NULL;
        }

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_MD_CTX_init(&ctx->mdctx);
	EVP_DigestInit_ex(&ctx->mdctx, md, NULL);
#else
	EVP_DigestInit(&ctx->mdctx, md);
#endif

	return ctx;
}

int digest_data(digestctx_t *ctx, char *buf, int buflen)
{
	EVP_DigestUpdate(&ctx->mdctx, buf, buflen);
	return 0;
}

char *digest_done(digestctx_t *ctx)
{
	char md_string[2*EVP_MAX_MD_SIZE+128];
	unsigned char md_value[EVP_MAX_MD_SIZE];
	int md_len, i;
	char *p;
	char *result;

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_DigestFinal_ex(&ctx->mdctx, md_value, &md_len);
#else
	EVP_DigestFinal(&ctx->mdctx, md_value, &md_len);
#endif

	sprintf(md_string, "%s:", ctx->digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) {
		p += sprintf(p, "%02x", md_value[i]);
		*p = '\0';
	}

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	EVP_MD_CTX_cleanup(&ctx->mdctx);
#else
	EVP_cleanup();
#endif

	free(ctx);

	result = (char *) malloc(strlen(md_string)+1);
	strcpy(result, md_string);

	return result;
}

#ifdef STANDALONE
int main(int argc, char *argv[])
{
	FILE *fd;
	char buf[8192];
	int buflen;
	digestctx_t *ctx;

	if (argc < 2) {
		printf("Usage: %s digestmethod [filename]\n", argv[0]);
		printf("\"digestmethod\" is usually \"md5\" or \"sha1\"\n");
		printf("Refer to the openssl help for more digestmethods\n");
		return 1;
	}

	if ((ctx = digest_init(argv[1])) == NULL) {
		printf("Unknown message digest method %s\n", argv[1]);
		return 1;
	}

	if (argc > 2) fd = fopen(argv[2], "r"); else fd = stdin;

	if (fd == NULL) {
		printf("Cannot open file %s\n", argv[2]);
		return 1;
	}

	while ((buflen = fread(buf, 1, sizeof(buf), fd)) > 0) {
		digest_data(ctx, buf, buflen);
	}

	printf("%s\n", digest_done(ctx));

	return 0;
}
#endif

