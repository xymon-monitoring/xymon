/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the message digest functions.                    */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DIGEST_H_
#define __DIGEST_H_

typedef struct digestctx_t {
	char *digestname;
	void *mdctx;
} digestctx_t;

extern digestctx_t *digest_init(char *digest);
extern int digest_data(digestctx_t *ctx, char *buf, int buflen);
extern char *digest_done(digestctx_t *ctx);

#endif
