/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2008-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef _COMPRESSION_H_
#define _COMPRESSION_H_

extern char *compressionmarker;
extern int  compressionmarkersz;

enum compressiontype_t { COMP_ZLIB, COMP_LZO, COMP_LZ4, COMP_LZ4HC, COMP_GZIP, COMP_PLAIN, COMP_UNKNOWN } ;
extern int docompress;
extern enum compressiontype_t comptype;

extern enum compressiontype_t parse_compressiontype(char *c);
extern const char * comptype2str(enum compressiontype_t ctype);
extern void *setup_compression_opts(void);

extern strbuffer_t *uncompress_message(enum compressiontype_t ctype, const char *datasrc, size_t datasz, size_t expandedsz, strbuffer_t *deststrbuffer, void *buffermemory);

extern void *uncompress_stream_init(void);
extern strbuffer_t *uncompress_stream_data(void *s, char *cmsg, size_t clen);
extern void uncompress_stream_done(void *s);

extern strbuffer_t *uncompress_to_my_buffer(const char *msg, size_t msglen, strbuffer_t *buf);
extern strbuffer_t *uncompress_buffer(const char *msg, size_t msglen, char *prestring);


extern strbuffer_t *compress_message_to_strbuffer(enum compressiontype_t ctype, const char *datasrc, size_t datasz, strbuffer_t *deststrbuffer, void *buffermemory);
extern strbuffer_t *compress_buffer(strbuffer_t *cmsg, char *msg, size_t msglen);

#endif

