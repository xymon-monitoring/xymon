/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is used to implement message digest functions (MD5, SHA1 etc.)        */
/*                                                                            */
/* Copyright (C) 2003-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: digest.c 6125 2009-02-12 13:09:34Z storner $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libbbgen.h"

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
		p += sprintf(p, "%02x", md_value[i]);
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


char *digest_done(digestctx_t *ctx)
{
	unsigned int md_len = 0;
	unsigned char *md_value = NULL;
	char *md_string = NULL;
	int i;
	char *p;

	switch (ctx->digesttype) {
	  case D_MD5:
		/* Built in MD5 hash */
		md_len = 16;
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		myMD5_Final(md_value, ctx->mdctx);
		break;
	  case D_SHA1:
		/* Built in SHA1 hash */
		md_len = 20;
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		mySHA1_Final(md_value, ctx->mdctx);
		break;
	  case D_RMD160:
		/* Built in RMD160 hash */
		md_len = 20;
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		myRIPEMD160_Final(md_value, ctx->mdctx);
		break;
	  case D_SHA512:
		/* Built in SHA-512 hash */
		md_len = (512/8);
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		mySHA512_Final(md_value, ctx->mdctx);
		break;
	  case D_SHA256:
		/* Built in SHA-256 hash */
		md_len = (256/8);
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		mySHA256_Final(md_value, ctx->mdctx);
		break;
	  case D_SHA384:
		/* Built in SHA-384 hash */
		md_len = (384/8);
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		mySHA384_Final(md_value, ctx->mdctx);
		break;
	  case D_SHA224:
		/* Built in SHA-224 hash */
		md_len = (224/8);
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		mySHA224_Final(md_value, ctx->mdctx);
		break;
	}

	sprintf(md_string, "%s:", ctx->digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) p += sprintf(p, "%02x", md_value[i]);
	*p = '\0';

	xfree(md_value);
	xfree(ctx->digestname);
	xfree(ctx->mdctx);
	xfree(ctx);

	return md_string;
}

