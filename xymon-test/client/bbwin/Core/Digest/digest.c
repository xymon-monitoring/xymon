/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is used to implement message digest functions (MD5, SHA1 etc.)        */
/*                                                                            */
/* Copyright (C) 2003-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: digest.c 70 2008-01-31 14:05:39Z sharpyy $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "digest.h"
#include "md5.h"
#include "sha1.h"

digestctx_t *digest_init(char *digest)
{
	struct digestctx_t *ctx = NULL;

	if (strcmp(digest, "md5") == 0) {
		/* Use the built in MD5 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = _strdup(digest);
		ctx->digesttype = D_MD5;
		ctx->mdctx = (void *)malloc(myMD5_Size());
		myMD5_Init(ctx->mdctx);
	}
	else if (strcmp(digest, "sha1") == 0) {
		/* Use the built in SHA1 routines */
		ctx = (digestctx_t *) malloc(sizeof(digestctx_t));
		ctx->digestname = _strdup(digest);
		ctx->digesttype = D_SHA1;
		ctx->mdctx = (void *)malloc(mySHA1_Size());
		mySHA1_Init(ctx->mdctx);
	}
	else {
		//errprintf("digest_init failure: Cannot handle digest %s\n", digest);
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
	}

	sprintf(md_string, "%s:", ctx->digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) p += sprintf(p, "%02x", md_value[i]);
	*p = '\0';

	free(md_value);
	free(ctx->digestname);
	free(ctx->mdctx);
	free(ctx);

	return md_string;
}

