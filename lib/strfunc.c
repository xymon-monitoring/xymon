/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains string handling routines.                                      */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: strfunc.c,v 1.9 2008-01-03 09:59:13 henrik Exp $";

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"
#include "version.h"

#define BUFSZINCREMENT 4096

strbuffer_t *newstrbuffer(int initialsize)
{
	strbuffer_t *newbuf;
	
	newbuf = calloc(1, sizeof(strbuffer_t));

	if (!initialsize) initialsize = 4096;

	newbuf->s = (char *)malloc(initialsize);
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
		/* Copy the NUL byte at end of newtext also */
		memcpy(buf->s+buf->used, newtext, newlen+1);
		buf->used += newlen;
	}
}

void addtobuffer(strbuffer_t *buf, char *newtext)
{
	strbuf_addtobuffer(buf, newtext, strlen(newtext));
}

void addtostrbuffer(strbuffer_t *buf, strbuffer_t *newtext)
{
	strbuf_addtobuffer(buf, STRBUF(newtext), STRBUFLEN(newtext));
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

