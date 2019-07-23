/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __STRFUNC_H__
#define __STRFUNC_H__

extern strbuffer_t *newstrbuffer(int initialsize);
extern strbuffer_t *convertstrbuffer(char *buffer, int bufsz);
extern void addtobuffer(strbuffer_t *buf, char *newtext);
extern void addtobuffer_many(strbuffer_t *buf, ...);
extern void addtostrbuffer(strbuffer_t *buf, strbuffer_t *newtext);
extern void addtobufferraw(strbuffer_t *buf, char *newdata, int bytes);
extern void clearstrbuffer(strbuffer_t *buf);
extern void freestrbuffer(strbuffer_t *buf);
extern char *grabstrbuffer(strbuffer_t *buf);
extern strbuffer_t *dupstrbuffer(char *src);
extern void strbufferchop(strbuffer_t *buf, int count);
extern void strbufferrecalc(strbuffer_t *buf);
extern void strbuffergrow(strbuffer_t *buf, int bytes);
extern void strbufferuse(strbuffer_t *buf, int bytes);
extern char *htmlquoted(char *s);
extern char *prehtmlquoted(char *s);
extern strbuffer_t *replacetext(char *original, char *oldtext, char *newtext);

#define SBUF_DEFINE(NAME) char *NAME = NULL; size_t NAME##_buflen = 0;
#define STATIC_SBUF_DEFINE(NAME) static char *NAME = NULL; static size_t NAME##_buflen = 0;
#define SBUF_MALLOC(NAME, LEN) { NAME##_buflen = (LEN); NAME = (char *)malloc((LEN)+1); }
#define SBUF_CALLOC(NAME, NMEMB, LEN) { NAME##_buflen = (LEN); NAME = (char *)calloc(NMEMB, (LEN)+1); }
#define SBUF_REALLOC(NAME, LEN) { NAME##_buflen = (LEN); NAME = (char *)realloc(NAME, (LEN)+1); }

/* How much can a string expand when htmlquoted? ' ' --> '&nbsp;' */
#define MAX_HTMLQUOTE_FACTOR 6

#endif

