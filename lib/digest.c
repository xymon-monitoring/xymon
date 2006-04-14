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

static char rcsid[] = "$Id: digest.c,v 1.13 2006-04-14 11:22:30 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libbbgen.h"

digestctx_t *digest_init(char *digest)
{
	struct digestctx_t *ctx = NULL;

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
		errprintf("digest_init failure: Cannot handle digest %s\n", digest);
		return NULL;
	}

	return ctx;
}


int digest_data(digestctx_t *ctx, unsigned char *buf, int buflen)
{
	switch (ctx->digesttype) {
	  case D_MD5:
		md5_append((md5_state_t *)ctx->mdctx, (const md5_byte_t *)buf, buflen);
		break;
	  case D_SHA1:
		mySHA1Update((mySHA1_CTX *)ctx->mdctx, buf, buflen);
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
		md5_finish((md5_state_t *)ctx->mdctx, md_value);
		break;
	  case D_SHA1:
		/* Built in SHA1 hash */
		md_len = 20;
		md_value = (unsigned char *)malloc(md_len*sizeof(unsigned char));
		md_string = (char *)malloc((2*md_len + strlen(ctx->digestname) + 2)*sizeof(char));
		mySHA1Final(md_value, (mySHA1_CTX *)ctx->mdctx);
		break;
	}

	sprintf(md_string, "%s:", ctx->digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) p += sprintf(p, "%02x", md_value[i]);
	*p = '\0';

	xfree(md_value);
	xfree(ctx->mdctx);
	xfree(ctx);

	return md_string;
}

