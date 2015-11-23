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

static char rcsid[] = "$Id: compression.c 6959 2012-05-01 19:55:12Z storner $";

#include "config.h"

#include <string.h>
#include <stdlib.h>


/*** ZLIB ***************************************************/
#include <zlib.h>

/* https://github.com/git/git/commit/225a6f1068f71723a910e8565db4e252b3ca21fa#diff-5541667b7def370db8c6fd8c0e9a93ef */
#if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
#define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif


/*** LZO ****************************************************/

#ifdef HAVE_LZO
#include <lzo/lzoconf.h>
#include <lzo/lzo1x.h>
#else
#include "minilzo.h"
#endif

#define IN_LEN      (128*1024ul)
#define OUT_LEN     (IN_LEN + IN_LEN / 16 + 64 + 3)
static unsigned char __LZO_MMODEL in  [ IN_LEN ];
static unsigned char __LZO_MMODEL out [ OUT_LEN ];

/* Work-memory needed for compression. Allocate memory in units
 * of 'lzo_align_t' (instead of 'char') to make sure it is properly aligned.
 */

#define HEAP_ALLOC(var,size) \
    lzo_align_t __LZO_MMODEL var [ ((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t) ]

static HEAP_ALLOC(wrkmem, LZO1X_1_MEM_COMPRESS);


/*** LZ4 ****************************************************/

#ifdef HAVE_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif



#include "libxymon.h"

/* Compress things by default? */
int enablecompression = 0;
/* Take from XYMON_COMPRESS variable at sendmsg time */
enum compressiontype_t defaultcompression = COMP_UNKNOWN;


/* enum compressiontype_t { COMP_ZLIB, COMP_LZO, COMP_GZIP, COMP_LZO2A, COMP_MINILZO, COMP_LZOP, COMP_LZ4, COMP_LZ4HC, COMP_PLAIN, COMP_UNKNOWN } */
enum compressiontype_t parse_compressiontype(char *c)
{

	if (!c) { errprintf("Empty compression type found\n"); return COMP_UNKNOWN; }

	return
		(strncasecmp(c, "lz4hc", 5) == 0) ? COMP_LZ4HC	:	// slow, high ratio compression; MAX decompression speed -- best for 'xymon' client
		(strncasecmp(c, "lz4", 3) == 0) ? COMP_LZ4	:	// sparse but fast compression; extremely fast decompression -- best for xymonnet/xymonproxy/xymond_client/rrd
		(strncasecmp(c, "lzo", 3) == 0) ? COMP_LZO	:	// default for sending; fast compression and decompression
		(strncasecmp(c, "zlib", 4) == 0) ? COMP_ZLIB	:	// compatibility with trunk / vanilla installs
		(strncasecmp(c, "gzip", 4) == 0) ? COMP_GZIP	:	// allows gzip -9 |  
		(strncasecmp(c, "plain", 4) == 0) ? COMP_PLAIN	:	// plaintext (no compression, but adds 'compress:' header w/ data size)
		(strncasecmp(c, "none", 4) == 0) ? COMP_PLAIN	:	// deprecated. to be removed
	COMP_UNKNOWN;
}

const char * comptype2str(enum compressiontype_t ctype)
{
	switch (ctype) {
	  case COMP_LZO:	return "lzo";
	  case COMP_LZ4:	return "lz4";
	  case COMP_LZ4HC:	return "lz4hc";
	  case COMP_ZLIB:	return "zlib";
	  case COMP_GZIP:	return "gzip";
	  case COMP_PLAIN:	return "plain";
	  case COMP_UNKNOWN:	return "<unknown>";
	}

	errprintf("Unrecognized compression_type given\n");
	return "<error>";
}


void *uncompress_stream_init(void)
{
	z_stream *strm;

	strm = (z_stream *)malloc(sizeof(z_stream));
	strm->zalloc = Z_NULL;
	strm->zfree = Z_NULL;
	strm->opaque = Z_NULL;
	strm->avail_in = 0;
	strm->next_in = Z_NULL;
	strm->avail_out = 0;
	strm->next_out = Z_NULL;
	if (inflateInit(strm) != Z_OK) {
		xfree(strm);
		return NULL;
	}

	return strm;
}

strbuffer_t *uncompress_stream_data(void *s, char *cmsg, size_t clen)
{
	z_stream *strm = (z_stream *)s;
	strbuffer_t *dbuf = NULL;
	int n;
	size_t nbytes;

	dbuf = newstrbuffer(4096);
	strm->avail_in = clen;
	strm->next_in = cmsg;

	do {
		strm->avail_out = STRBUFSZ(dbuf) - STRBUFLEN(dbuf);
		if (strm->avail_out < 4096) {
			if (strbuffergrow(dbuf, 4096) == -1) { freestrbuffer(dbuf); return NULL; }
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


strbuffer_t *uncompress_to_my_buffer(const char *msg, size_t msglen, strbuffer_t *dbuf)
{
	static z_stream *strm = NULL;
	int n;
	size_t nbytes, avbytes;


	if (!msglen) return NULL;
	if (!strm) {
		strm = uncompress_stream_init();
		if (!strm) return NULL;
	}
	else {
		inflateReset(strm);	/* We'll reuse the strm struct */
	}

	do {
		strm->avail_in = msglen;
		strm->next_in = (char *)msg;

		do {
			avbytes = STRBUFSZ(dbuf) - STRBUFLEN(dbuf) - 1;	/* -1 for the trailing \0 */
			if (strm->avail_out < 4096) {
				if (strbuffergrow(dbuf, 4096) == -1) { n = Z_MEM_ERROR; goto done; }
				avbytes = STRBUFSZ(dbuf) - STRBUFLEN(dbuf) - 1;
			}

			strm->avail_out = avbytes;
			strm->next_out = STRBUF(dbuf) + STRBUFLEN(dbuf);
			n = inflate(strm, Z_NO_FLUSH);

			switch (n) {
			  case Z_STREAM_ERROR:
				xfree(strm); strm = NULL;
				goto done;
			  case Z_NEED_DICT:
			  case Z_DATA_ERROR:
			  case Z_MEM_ERROR:
				goto done;
			}
			nbytes = avbytes - strm->avail_out;
			strbufferuse(dbuf, nbytes);
		} while (strm->avail_out == 0);
	} while (n != Z_STREAM_END);

done:
	if (n == Z_STREAM_END) {
		dbgprintf("Inflated message from %d to %d bytes (%d %%)\n", msglen, STRBUFLEN(dbuf), 100*STRBUFLEN(dbuf)/msglen-100);
		return dbuf;
	}
	else {
		errprintf("ERROR %d zlib-inflating %zu bytes message\n", n, msglen);
		return NULL;
	}
}


/* Compatibility, but with saner memory management */
strbuffer_t *uncompress_buffer(const char *msg, size_t msglen, char *prestring)
{
	static size_t dbufmax = 0;
	strbuffer_t *dbuf;

	if (dbufmax < 2*msglen) dbufmax = 2*msglen;
	dbuf = newstrbuffer(dbufmax);
	if (prestring) addtobuffer(dbuf, prestring);

	if (uncompress_to_my_buffer(msg, msglen, dbuf) == NULL) {
		/* Something happened, but we're responsible for this strbuf_t */
		freestrbuffer(dbuf);
		return NULL;
	}

	return dbuf;
}


/*
 * Handle uncompressing a buffer that's already had metadata discovered. We know the compression type, 
 * current size, expected size, and the caller is handling memory management.
 * If coming from xymond, it's already parsed the "compress:xyz 12345\n" header
 */

strbuffer_t *uncompress_message(enum compressiontype_t ctype, const char *datasrc, size_t datasz, size_t expandedsz, strbuffer_t *deststrbuffer, void *buffermemory)
{

	if (ctype == COMP_LZO) {
		static int lzo_inited = 0;
		int result;

		if (!lzo_inited && (lzo_init() != LZO_E_OK)) {
			errprintf("uncompress_message(): Could not do LZO init!\n");
			return NULL;
		}

		result = lzo1x_decompress(datasrc, datasz, STRBUF(deststrbuffer), (lzo_uintp)&expandedsz, NULL);
		if (result != LZO_E_OK) {
			errprintf("LZO_decompression failed!\n");
			return NULL;
		}
		dbgprintf(" - lzo_decompressed %zd bytes into %zd\n", datasz, expandedsz);
		deststrbuffer->used += expandedsz; // manual OK here? /* don't forget the \0 */
		return deststrbuffer;
	}
#ifdef HAVE_LZ4
	else if ((ctype == COMP_LZ4HC) || (ctype == COMP_LZ4)) {
		int newsize;

		newsize = LZ4_decompress_safe_partial(datasrc, STRBUFEND(deststrbuffer), datasz, expandedsz, expandedsz);
		if (newsize <= 0) {
			errprintf("LZ4_decompression failed!\n");
			return NULL;
		}
		dbgprintf(" - lz4_decompressed %zd bytes into %d\n", datasz, newsize);
		deststrbuffer->used += newsize;	// manual OK here? /* don't forget the \0 */
		return deststrbuffer;
	}
#endif
	else if (ctype == COMP_ZLIB) {
		// this should be more generic
		return uncompress_to_my_buffer(datasrc, datasz, deststrbuffer);
	}
	else if (ctype == COMP_PLAIN) {
		strbuf_addtobuffer(deststrbuffer, (char *)datasrc, datasz);
		return deststrbuffer;
	}
	else {
		errprintf("uncompress_message(): don't know how to decompress %s yet... sorry\n", comptype2str(ctype));
		return NULL;
	}
}


strbuffer_t *compress_message_to_strbuffer(enum compressiontype_t ctype, const char *datasrc, size_t datasz, strbuffer_t *deststrbuffer, void *buffermemory)
{
	char compressionhdr[30];

	dbgprintf(" -> compress_message_to_strbuffer\n");

	sprintf(compressionhdr, "compress:%s %zu\n", comptype2str(ctype), datasz);
	if (deststrbuffer == NULL) {
		/*
		 * Guess we're making a new one. Account for uncompressable messages by allocating to the worst case scenario.
		 * our own header should be about 30 extra bytes.
		 * 
		 * LZ4 : LZ4_COMPRESSBOUND(isize) ((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize)/255) + 16)
		 * LZO : output_block_size = input_block_size + (input_block_size / 16) + 64 + 3
		 #   LZO2A (not used) : LZO_EXTRA_BUFFER(len) ((len)/8 + 128 + 3)
		 * ZLIB : complen = sourceLen + ((sourceLen + 7) >> 3) + ((sourceLen + 63) >> 6) + 5 + 6;
		 * 
		 * At this point, it's "cheapest" to use LZO's value. I'd recommend zlib users move to lzo regardless.
		 * Another option is to use MAX_XYMON_INBUFSZ and be done with it, and that would be best for a persistent strbuffer
		 * but at the moment this is a transient buffer and we don't want to be doing large malloc's unnecessarily.
		 */
		deststrbuffer = newstrbuffer(datasz + (datasz / 16) + 64 + 3 + 30);

		*(STRBUF(deststrbuffer) + sizeof(long)) = '\0'; /* prevent valgrind complaint about eventually sending this via bfq uninitialized */
	}
	addtobuffer(deststrbuffer, compressionhdr);

	if (ctype == COMP_LZO) {
		static int lzo_inited = 0;
		size_t newsize = 0;
		int result;

		if (!lzo_inited && (lzo_init() != LZO_E_OK)) {
			errprintf("compress_message(): Could not do LZO init!\n");
			return NULL;
		}

		result = lzo1x_1_compress(datasrc, datasz, STRBUFEND(deststrbuffer), (lzo_uintp)&newsize, wrkmem);
		if (result != LZO_E_OK) {
			errprintf("LZO_compression failed!\n");
			return NULL;
		}
		dbgprintf(" - lzo_compressed %zu bytes into %zu\n", datasz, newsize);
		deststrbuffer->used += newsize;	/* don't forget the \0 */
		return deststrbuffer;
	}
#ifdef HAVE_LZ4
	else if (ctype == COMP_LZ4HC) {
		int newsize = 0;

		newsize = LZ4_compressHC(datasrc, STRBUFEND(deststrbuffer), datasz);
		if (newsize <= 0) {
			errprintf("LZ4HC_compression failed!\n");
			return NULL;
		}
		dbgprintf(" - lz4hc_compressed %zu bytes into %d\n", datasz, newsize);
		deststrbuffer->used += newsize;	/* don't forget the \0 */
		return deststrbuffer;
	}
	else if (ctype == COMP_LZ4) {
		int newsize = 0;

		newsize = LZ4_compress(datasrc, STRBUFEND(deststrbuffer), datasz);
		if (newsize <= 0) {
			errprintf("LZ4_compression failed!\n");
			return NULL;
		}
		dbgprintf(" - lz4_compressed %zu bytes into %d\n", datasz, newsize);
		deststrbuffer->used += newsize;	/* don't forget the \0 */
		return deststrbuffer;
	}
#endif
	else if (ctype == COMP_ZLIB) {
		// this should be more generic
		(void *)compress_buffer(deststrbuffer, (char *)datasrc, datasz);
		return deststrbuffer;
	}
	else if (ctype == COMP_PLAIN) {
		strbuf_addtobuffer(deststrbuffer, (char *)datasrc, datasz);
		return deststrbuffer;
	}
	else {
		errprintf("compress_message(): don't know how to compress %s yet... sorry\n", comptype2str(ctype));
		return NULL;
	}
}



strbuffer_t *compress_buffer(strbuffer_t *cmsg, char *msg, size_t msglen)
{
	z_stream *strm = NULL;
	int ret;
	size_t sz;
	// char compressionhdr[30];

	dbgprintf(" -> compress_buffer\n");


	if (!msglen) return NULL;
	if (defaultcompression != COMP_ZLIB) errprintf("Asked to compress in a format we don't understand? You're getting zlib.\n");

	strm = (z_stream *)calloc(1, sizeof(z_stream));
	strm->zalloc = Z_NULL;
	strm->zfree = Z_NULL;
	strm->opaque = Z_NULL;
	ret = deflateInit(strm, Z_DEFAULT_COMPRESSION);

	if (ret != Z_OK) {
		xfree(strm);
		return NULL;
	}

	// sprintf(compressionhdr, "compress:zlib %zu\n", msglen);

	/* How much output space will be needed? */
	strm->avail_in = msglen;
	strm->next_in = msg;
	sz = strm->avail_out = deflateBound(strm, msglen);
	// dbgprintf("DeflateBound returned %zu, compressionhdr is %zu\n", strm->avail_out, strlen(compressionhdr));

	// cmsg = newstrbuffer(strlen(compressionhdr) + strm->avail_out);
	// addtobuffer(cmsg, compressionhdr);

	/* Setting destination to buffer + headerlength */
	// dbgprintf("cmsg buffer in use=%zu\n", STRBUFLEN(cmsg));
	strm->next_out = STRBUF(cmsg) + STRBUFLEN(cmsg);

	ret = deflate(strm, Z_FINISH);
	dbgprintf("Compression returned=%d; avail_in=%u, avail_out=%u, cmsglengthtonull=%zu, expected delta=%tu\n", ret, strm->avail_in, strm->avail_out, strlen(STRBUF(cmsg)), (sz - strm->avail_out));
	if ((ret != Z_STREAM_ERROR) && (strm->avail_in == 0) && msglen) {
		/* All was compressed OK */
		strbufferuse(cmsg, (sz - strm->avail_out));
		dbgprintf("Compressed message from %zd to %zd bytes (%d %%)\n", msglen, STRBUFLEN(cmsg), 100 - (100*STRBUFLEN(cmsg)/msglen));
	}
	else {
		errprintf("Compression failed: ret=%d, avail_in=%u, avail_out=%u\n", ret, strm->avail_in, strm->avail_out);
		freestrbuffer(cmsg); cmsg = NULL;
	}

	// dbgprintf("Compressed Message Data:\n%s\n", STRBUF(cmsg));
	deflateEnd(strm);
	xfree(strm);

	dbgprintf(" <- compress_buffer\n");

	return cmsg;
}


