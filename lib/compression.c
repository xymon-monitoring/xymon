/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for interfacing to zlib compression routines.         */
/*                                                                            */
/* Copyright (C) 2008-2012 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: compression.c,v 1.2 2008/03/02 18:37:28 henrik Exp $";

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <zlib.h>

#include "libxymon.h"

void *uncompress_stream_init(void)
{
	z_stream *strm;

	strm = (z_stream *)malloc(sizeof(z_stream));
	strm->zalloc = Z_NULL;
	strm->zfree = Z_NULL;
	strm->opaque = Z_NULL;
	strm->avail_in = 0;
	strm->next_in = Z_NULL;
	if (inflateInit(strm) != Z_OK) {
		xfree(strm);
		return NULL;
	}

	return strm;
}

strbuffer_t *uncompress_stream_data(void *s, char *cmsg, int clen)
{
	z_stream *strm = (z_stream *)s;
	strbuffer_t *dbuf = NULL;
	int n;
	unsigned int nbytes;

	dbuf = newstrbuffer(4096);
	strm->avail_in = clen;
	strm->next_in = cmsg;

	do {
		strm->avail_out = STRBUFSZ(dbuf) - STRBUFLEN(dbuf);
		if (strm->avail_out < 4096) {
			strbuffergrow(dbuf, 4096);
			strm->avail_out = STRBUFSZ(dbuf) - STRBUFLEN(dbuf);
		}
		strm->next_out = STRBUF(dbuf) + STRBUFLEN(dbuf);

		n = inflate(strm, Z_NO_FLUSH);
		switch (n) {
		  case Z_STREAM_ERROR:
		  case Z_NEED_DICT:
		  case Z_DATA_ERROR:
		  case Z_MEM_ERROR:
			freestrbuffer(dbuf);
			return NULL;
		}
		nbytes = STRBUFSZ(dbuf) - STRBUFLEN(dbuf) - strm->avail_out;
		strbufferuse(dbuf, nbytes);
	} while (strm->avail_out == 0);

	return dbuf;
}


void uncompress_stream_done(void *s)
{
	inflateEnd((z_stream *)s);
	xfree(s);
}


strbuffer_t *uncompress_buffer(char *msg, int msglen, char *prestring)
{
	static z_stream *strm = NULL;
	strbuffer_t *dbuf;
	static int dbufmax = 0;
	int n;
	unsigned int nbytes, avbytes;

	if (!strm) {
		strm = uncompress_stream_init();
		if (!strm) return NULL;
	}
	else {
		inflateReset(strm);	/* We'll reuse the strm struct */
	}

	if (dbufmax < 2*msglen) dbufmax = 2*msglen;
	dbuf = newstrbuffer(dbufmax);
	if (prestring) addtobuffer(dbuf, prestring);

	do {
		strm->avail_in = msglen;
		strm->next_in = msg;

		do {
			avbytes = STRBUFSZ(dbuf) - STRBUFLEN(dbuf) - 1;	/* -1 for the trailing \0 */
			if (strm->avail_out < 4096) {
				strbuffergrow(dbuf, 4096);
				avbytes = STRBUFSZ(dbuf) - STRBUFLEN(dbuf) - 1;
			}

			strm->avail_out = avbytes;
			strm->next_out = STRBUF(dbuf) + STRBUFLEN(dbuf);
			n = inflate(strm, Z_NO_FLUSH);

			switch (n) {
			  case Z_STREAM_ERROR:
				xfree(strm); strm = NULL;
				freestrbuffer(dbuf);
				return NULL;
			  case Z_NEED_DICT:
			  case Z_DATA_ERROR:
			  case Z_MEM_ERROR:
				n = Z_DATA_ERROR;
				goto done;
			}
			nbytes = avbytes - strm->avail_out;
			strbufferuse(dbuf, nbytes);
		} while (strm->avail_out == 0);
	} while (n != Z_STREAM_END);

done:
	if (n == Z_STREAM_END) {
		// errprintf("Inflated message from %d to %d bytes (%d %%)\n", msglen, STRBUFLEN(dbuf), 100*STRBUFLEN(dbuf)/msglen-100);
		return dbuf;
	}
	else {
		freestrbuffer(dbuf);
		return NULL;
	}
}




strbuffer_t *compress_buffer(char *msg, int msglen)
{
	z_stream *strm = NULL;
	int ret;
	unsigned int sz;
	strbuffer_t *cmsg;
	char compressionhdr[30];

	strm = (z_stream *)calloc(1, sizeof(z_stream));
	strm->zalloc = Z_NULL;
	strm->zfree = Z_NULL;
	strm->opaque = Z_NULL;
	ret = deflateInit(strm, Z_DEFAULT_COMPRESSION);

	if (ret != Z_OK) {
		xfree(strm);
		return NULL;
	}

	sprintf(compressionhdr, "compress:zlib %d\n", msglen);

	/* How much output space will be needed? */
	strm->avail_in = msglen;
	strm->next_in = msg;
	sz = strm->avail_out = deflateBound(strm, msglen);

	cmsg = newstrbuffer(strlen(compressionhdr) + strm->avail_out);
	addtobuffer(cmsg, compressionhdr);

	strm->next_out = STRBUF(cmsg) + STRBUFLEN(cmsg);

	ret = deflate(strm, Z_FINISH);
	if ((ret != Z_STREAM_ERROR) && (strm->avail_in == 0)) {
		/* All was compressed OK */
		strbufferuse(cmsg, (sz - strm->avail_out));
		// errprintf("Compressed message from %d to %d bytes (%d %%)\n", msglen, STRBUFLEN(cmsg), 100 - (100*STRBUFLEN(cmsg)/msglen));
	}
	else {
		errprintf("Compression failed: ret=%d, avail_in=%u\n", ret, strm->avail_in);
		freestrbuffer(cmsg); cmsg = NULL;
	}

compressdone:
	deflateEnd(strm);
	xfree(strm);

	return cmsg;
}

