/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains string handling routines.                                      */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include "libxymon.h"
#include "version.h"

#define BUFSZINCREMENT 4096

strbuffer_t *newstrbuffer(int initialsize)
{
	strbuffer_t *newbuf;
	
	newbuf = calloc(1, sizeof(strbuffer_t));

	if (!initialsize) initialsize = 4096;

	newbuf->s = (char *)malloc(initialsize);
	if (newbuf->s == NULL) {
		errprintf("newstrbuffer: Attempt to allocate failed (initialsize=%d): %s\n", initialsize, strerror(errno));
		xfree(newbuf);
		return NULL;
	}
	*(newbuf->s) = '\0';
	newbuf->sz = initialsize;

	return newbuf;
}

strbuffer_t *convertstrbuffer(char *buffer, int bufsz)
{
	strbuffer_t *newbuf;
	
	newbuf = calloc(1, sizeof(strbuffer_t));
	newbuf->s = buffer;
	newbuf->used = strlen(buffer);
	newbuf->sz = (bufsz ? bufsz : newbuf->used+1);

	return newbuf;
}

void freestrbuffer(strbuffer_t *buf)
{
	if (buf == NULL) return;

	if (buf->s) xfree(buf->s);
	xfree(buf);
}

void clearstrbuffer(strbuffer_t *buf)
{
	if (buf == NULL) return;

	if (buf->s) {
		*(buf->s) = '\0';
		buf->used = 0;
	}
}

char *grabstrbuffer(strbuffer_t *buf)
{
	char *result;

	if (buf == NULL) return NULL;

	result = buf->s;
	xfree(buf);

	return result;
}

strbuffer_t *dupstrbuffer(char *src)
{
	strbuffer_t *newbuf;
	int len = 0;
	
	newbuf = newstrbuffer(0);
	if (src) {
		newbuf->s = strdup(src);
		len = strlen(src);
		newbuf->used = newbuf->sz = len;
	}

	return newbuf;
}

static void strbuf_addtobuffer(strbuffer_t *buf, char *newtext, int newlen)
{
	if (buf->s == NULL) {
		buf->used = 0;
		buf->sz = newlen + BUFSZINCREMENT;
		buf->s = (char *) malloc(buf->sz);
		*(buf->s) = '\0';
	}
	else if ((buf->used + newlen + 1) > buf->sz) {
		buf->sz += (newlen + BUFSZINCREMENT);
		buf->s = (char *) realloc(buf->s, buf->sz);
	}

	if (newtext) {
		memcpy(buf->s+buf->used, newtext, newlen);
		buf->used += newlen;
		/* Make sure result buffer is NUL-terminated; newtext might not be. */
		*(buf->s + buf->used) = '\0';
	}
}

void addtobuffer(strbuffer_t *buf, char *newtext)
{
	if (newtext) strbuf_addtobuffer(buf, newtext, strlen(newtext));
}

void addtobuffer_many(strbuffer_t *buf, ...)
{
	va_list ap;
	char *newtext;

	va_start(ap, buf);
	newtext = va_arg(ap, char *);
	while (newtext) {
		strbuf_addtobuffer(buf, newtext, strlen(newtext));
		newtext = va_arg(ap, char *);
	}
	va_end(ap);
}

void addtostrbuffer(strbuffer_t *buf, strbuffer_t *newtext)
{
	strbuf_addtobuffer(buf, STRBUF(newtext), STRBUFLEN(newtext));
}

void addtobufferraw(strbuffer_t *buf, char *newdata, int bytes)
{
	/* Add binary data to the buffer */
	strbuf_addtobuffer(buf, newdata, bytes);
}

void strbufferchop(strbuffer_t *buf, int count)
{
	/* Remove COUNT characters from end of buffer */
	if ((buf == NULL) || (buf->s == NULL)) return;

	if (count >= buf->used) count = buf->used;

	buf->used -= count;
	*(buf->s+buf->used) = '\0';
}

void strbufferrecalc(strbuffer_t *buf)
{
	if (buf == NULL) return;

	if (buf->s == NULL) {
		buf->used = 0;
		return;
	}

	buf->used = strlen(buf->s);
}

void strbuffergrow(strbuffer_t *buf, int bytes)
{
	if (buf == NULL) return;

	buf->sz += bytes;
	buf->s = (char *) realloc(buf->s, buf->sz);
}

void strbufferuse(strbuffer_t *buf, int bytes)
{
	if (buf == NULL) return;

	if ((buf->used + bytes) < buf->sz) {
		buf->used += bytes;
	}
	else {
		errprintf("strbuffer: Attempt to use more than allocated (sz=%d, used=%d, growbytes=%d\n", 
			  buf->sz, buf->used, bytes);
	}
	*(buf->s+buf->used) = '\0';
}

char *htmlquoted(char *s)
{
	/*
	 * This routine converts a plain string into an html-quoted string
	 */

	static strbuffer_t *result = NULL;
	char *inp, *endp;
	char c;

	if (!s) return NULL;

	if (!result) result = newstrbuffer(4096);
	clearstrbuffer(result);

	inp = s;
	do {
		endp = inp + strcspn(inp, "\"&<> ");
		c = *endp;
		if (endp > inp) addtobufferraw(result, inp, endp-inp);
		switch (c) {
		  case '"': addtobuffer(result, "&quot;"); break;
		  case '&': addtobuffer(result, "&amp;"); break;
		  case '<': addtobuffer(result, "&lt;"); break;
		  case '>': addtobuffer(result, "&gt;"); break;
		  case ' ': addtobuffer(result, "&nbsp;"); break;
		  default: break;
		}
		inp = (c == '\0') ? NULL : endp+1;
	} while (inp);

	return STRBUF(result);
}

char *prehtmlquoted(char *s)
{
	/*
	 * This routine converts a string which may contain html to a string
	 * safe to include in a PRE block. It's similar to above, but escapes
	 * only the minmum characters for efficiency.
	 */

	static strbuffer_t *result = NULL;
	char *inp, *endp;
	char c;

	if (!s) return NULL;

	if (!result) result = newstrbuffer(4096);
	clearstrbuffer(result);

	inp = s;
	do {
		endp = inp + strcspn(inp, "&<>");
		c = *endp;
		if (endp > inp) addtobufferraw(result, inp, endp-inp);
		switch (c) {
		  case '&': addtobuffer(result, "&amp;"); break;
		  case '<': addtobuffer(result, "&lt;"); break;
		  case '>': addtobuffer(result, "&gt;"); break;	// this is not, strictly speaking, needed, but unbalanced encoding might confuse automated readers
		  default: break;
		}
		inp = (c == '\0') ? NULL : endp+1;
	} while (inp);

	return STRBUF(result);
}

strbuffer_t *replacetext(char *original, char *oldtext, char *newtext)
{
	strbuffer_t *result = newstrbuffer(0);
	char *pos = original, *start;

	do {
		start = pos; pos = strstr(pos, oldtext);
		if (pos) {
			if (pos > start) strbuf_addtobuffer(result, start, (pos - start));
			addtobuffer(result, newtext);
			pos += strlen(oldtext);
		}
		else
			addtobuffer(result, start);
	} while (pos);

	return result;
}

