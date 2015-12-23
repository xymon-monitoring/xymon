/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains memory management routines.                                    */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "libxymon.h"

const char *xfreenullstr = "xfree: Trying to free a NULL pointer\n";

void *xcalloc(size_t nmemb, size_t size)
{
	void *result;

	result = calloc(nmemb, size);
	if (result == NULL) {
		errprintf("xcalloc: Out of memory!\n");
		abort();
	}

	return result;
}


void *xmalloc(size_t size)
{
	void *result;

	result = malloc(size);
	if (result == NULL) {
		errprintf("xmalloc: Out of memory!\n");
		abort();
	}

	return result;
}


void *xrealloc(void *ptr, size_t size)
{
	void *result;

	if (ptr == NULL) {
		errprintf("xrealloc: Cannot realloc NULL pointer\n");
		abort();
	}

	result = realloc(ptr, size);
	if (result == NULL) {
		errprintf("xrealloc: Out of memory!\n");
		abort();
	}

	return result;
}

char *xstrdup(const char *s)
{
	char *result;

	if (s == NULL) {
		errprintf("xstrdup: Cannot dup NULL string\n");
		abort();
	}

	result = strdup(s);
	if (result == NULL) {
		errprintf("xstrdup: Out of memory\n");
		abort();
	}

	return result;
}

char *xstrcat(char *dest, const char *src)
{
	if (src == NULL) {
		errprintf("xstrcat: NULL destination\n");
		abort();
	}

	if (dest == NULL) {
		errprintf("xstrcat: NULL destination\n");
		abort();
	}

	strcat(dest, src);
	return dest;
}

char *xstrncat(char *dest, const char *src, size_t maxlen)
{
	if (src == NULL) {
		errprintf("xstrncat: NULL destination\n");
		abort();
	}

	if (dest == NULL) {
		errprintf("xstrncat: NULL destination\n");
		abort();
	}

	strncat(dest, src, maxlen);
	return dest;
}

char *xstrcpy(char *dest, const char *src)
{
	if (src == NULL) {
		errprintf("xstrcpy: NULL destination\n");
		abort();
	}

	if (dest == NULL) {
		errprintf("xstrcpy: NULL destination\n");
		abort();
	}

	strcpy(dest, src);
	return dest;
}

char *xstrncpy(char *dest, const char *src, size_t maxlen)
{
	if (src == NULL) {
		errprintf("xstrncpy: NULL destination\n");
		abort();
	}

	if (dest == NULL) {
		errprintf("xstrncpy: NULL destination\n");
		abort();
	}

	strncpy(dest, src, maxlen);
	return dest;
}

int xsprintf(char *dest, const char *fmt, ...)
{
	va_list args;
	size_t printedbytes;

	if (dest == NULL) {
		errprintf("xsprintf: NULL destination\n");
		abort();
	}

	va_start(args, fmt);
	printedbytes = vsprintf(dest, fmt, args);
	va_end(args);

	return printedbytes;
}


char *xresultbuf(int maxsz)
{
	static char rrbuf[10000];
	static char *rrbufnext = rrbuf;
	char *result;

	if ((rrbufnext + maxsz) >= (rrbuf + sizeof(rrbuf))) 
		result = rrbufnext = rrbuf;
	else {
		result = rrbufnext;
		rrbufnext += maxsz;
	}

	return result;
}

