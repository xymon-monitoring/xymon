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

static char rcsid[] = "$Id: digest.c,v 1.1 2003-09-13 16:03:16 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>

#include "digest.h"

static char digestname[30];
static EVP_MD_CTX mdctx;
static int dgst_ready = 0;

int digest_init(char *digest)
{
	static int dgst_init_done = 0;
	const EVP_MD *md;

	if (!dgst_init_done) {
		OpenSSL_add_all_digests();
		dgst_init_done = 1;
	}

	strncpy(digestname, digest, sizeof(digestname)-1);
	digestname[sizeof(digestname)-1] = '\0';
	md = EVP_get_digestbyname(digestname);

	if (!md) {
		return 1;
        }

	EVP_DigestInit(&mdctx, md);

	dgst_ready = 1;

	return 0;
}

int digest_data(char *buf, int buflen)
{
	if (!dgst_ready) return 1;

	EVP_DigestUpdate(&mdctx, buf, buflen);
	return 0;
}

char *digest_done(void)
{
	static char md_string[2*EVP_MAX_MD_SIZE+128];
	unsigned char md_value[EVP_MAX_MD_SIZE];
	int md_len, i;
	char *p;

	if (!dgst_ready) return NULL;

	EVP_DigestFinal(&mdctx, md_value, &md_len);

	sprintf(md_string, "%s:", digestname);
	for(i = 0, p = md_string + strlen(md_string); (i < md_len); i++) {
		p += sprintf(p, "%02x", md_value[i]);
		*p = '\0';
	}

	dgst_ready = 0;
	return md_string;
}

#ifdef STANDALONE
int main(int argc, char *argv[])
{
	FILE *fd;
	char buf[8192];
	int buflen;

	if (argc < 2) {
		printf("Usage: %s digestmethod [filename]\n", argv[0]);
		printf("\"digestmethod\" is usually \"md5\" or \"sha1\"\n");
		printf("Refer to the openssl help for more digestmethods\n");
		return 1;
	}

	if (digest_init(argv[1]) != 0) {
		printf("Unknown message digest method %s\n", argv[1]);
		return 1;
	}

	if (argc > 2) fd = fopen(argv[2], "r"); else fd = stdin;

	if (fd == NULL) {
		printf("Cannot open file %s\n", argv[2]);
		return 1;
	}

	while ((buflen = fread(buf, 1, sizeof(buf), fd)) > 0) {
		digest_data(buf, buflen);
	}

	printf("%s\n", digest_done());

	EVP_cleanup();
	return 0;
}
#endif

