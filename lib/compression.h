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

extern void *uncompress_stream_init(void);
extern strbuffer_t *uncompress_stream_data(void *s, char *cmsg, int clen);
extern void uncompress_stream_done(void *s);

extern strbuffer_t *uncompress_buffer(char *msg, int msglen, char *prestring);
extern strbuffer_t *compress_buffer(char *msg, int msglen);

#endif

