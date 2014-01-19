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

#endif

