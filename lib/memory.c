/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains memory management routines.                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: memory.c,v 1.11 2006-07-09 13:49:00 henrik Exp $";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#define LIB_MEMORY_C_COMPILE 1

#include "libbbgen.h"

#ifdef MEMORY_DEBUG
static xmemory_t *mhead = NULL;
static xmemory_t *dmem;
static void *allocend, *copyend;
#endif

#ifdef MEMORY_DEBUG
void add_to_memlist(void *ptr, size_t memsize)
{
	xmemory_t *newitem;

	newitem = (xmemory_t *)malloc(sizeof(xmemory_t));
	newitem->sdata = ptr;
	newitem->ssize = memsize;
	newitem->next = mhead;
	mhead = newitem;
}

static void dump_memitems(void)
{
	xmemory_t *mwalk;

	for (mwalk = mhead; (mwalk); mwalk = mwalk->next) {
		errprintf("%8x  : %5d\n", mwalk->sdata, mwalk->ssize);
	}
}

static xmemory_t *find_in_memlist(void *ptr)
{
	xmemory_t *mwalk = mhead;
	int found = 0;

	while (mwalk) {
		found = (((void *)ptr >= (void *)mwalk->sdata) && ((void *)ptr < (void *)(mwalk->sdata + mwalk->ssize)));
		if (found) return mwalk;

		mwalk = mwalk->next;
	}

	return NULL;
}


void remove_from_memlist(void *ptr)
{
	xmemory_t *mwalk, *mitem;
	
	if (ptr == NULL) {
		errprintf("remove_from_memlist called with NULL pointer\n");
		dump_memitems();
		abort();
	}

	mitem= find_in_memlist(ptr);
	if (mitem == NULL) {
		errprintf("remove_from_memlist called with bogus pointer\n");
		abort();
	}

	if (mitem == mhead) {
		mhead = mhead->next;
		free(mitem);
	}
	else {
		for (mwalk = mhead; (mwalk->next != mitem); mwalk = mwalk->next) ;
		mwalk->next = mitem->next;
		free(mitem);
	}
}
#endif

const char *xfreenullstr = "xfree: Trying to free a NULL pointer\n";

void *xcalloc(size_t nmemb, size_t size)
{
	void *result;

	result = calloc(nmemb, size);
	if (result == NULL) {
		errprintf("xcalloc: Out of memory!\n");
		abort();
	}

#ifdef MEMORY_DEBUG
	add_to_memlist(result, nmemb*size);
#endif
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

#ifdef MEMORY_DEBUG
	add_to_memlist(result, size);
#endif
	return result;
}


void *xrealloc(void *ptr, size_t size)
{
	void *result;

	if (ptr == NULL) {
		errprintf("xrealloc: Cannot realloc NULL pointer\n");
		abort();
	}

#ifdef MEMORY_DEBUG
	dmem = find_in_memlist(ptr);
	if (dmem == NULL) {
		errprintf("xrealloc: Called with bogus pointer\n");
		abort();
	}
#endif

	result = realloc(ptr, size);
	if (result == NULL) {
		errprintf("xrealloc: Out of memory!\n");
		abort();
	}

#ifdef MEMORY_DEBUG
	dmem->sdata = result;
	dmem->ssize = size;
#endif

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

#ifdef MEMORY_DEBUG
	add_to_memlist(result, strlen(result)+1);
#endif

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

#ifdef MEMORY_DEBUG
	dmem = find_in_memlist(dest);
	if (dmem == NULL) {
		errprintf("xstrcat: Bogus destination\n");
		abort();
	}

	allocend = dmem->sdata + dmem->ssize - 1;
	copyend = dest + strlen(dest) + strlen(src);
	if ((void *)copyend > (void *)allocend) {
		errprintf("xstrcat: Overwrite of %d bytes\n", (copyend - allocend));
		abort();
	}
#endif

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

#ifdef MEMORY_DEBUG
	dmem = find_in_memlist(dest);
	if (dmem == NULL) {
		errprintf("xstrncat: Bogus destination\n");
		abort();
	}

	allocend = dmem->sdata + dmem->ssize - 1;
	if (strlen(src) <= maxlen)
		copyend = dest + strlen(dest) + strlen(src);
	else
		copyend = dest + strlen(dest) + maxlen;

	if ((void *)copyend > (void *)allocend) {
		errprintf("xstrncat: Potential overwrite of %d bytes\n", (copyend - allocend));
		abort();
	}

	if (strlen(dest) + strlen(src) >= maxlen) {
		errprintf("xstrncat: destination is not NULL terminated - dst '%s', src '%s', max %d\n", dest, src, maxlen);
	}
#endif

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

#ifdef MEMORY_DEBUG
	dmem = find_in_memlist(dest);
	if (dmem == NULL) {
		errprintf("xstrcpy: Bogus destination\n");
		abort();
	}

	allocend = dmem->sdata + dmem->ssize - 1;
	copyend = dest + strlen(src);
	if ((void *)copyend > (void *)allocend) {
		errprintf("xstrcpy: Overwrite of %d bytes\n", (copyend - allocend));
		abort();
	}
#endif

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

#ifdef MEMORY_DEBUG
	dmem = find_in_memlist(dest);
	if (dmem == NULL) {
		errprintf("xstrncpy: Bogus destination\n");
		abort();
	}

	allocend = dmem->sdata + dmem->ssize - 1;
	if (strlen(src) <= maxlen)
		copyend = dest + strlen(src);
	else
		copyend = dest + maxlen;
	if ((void *)copyend > (void *)allocend) {
		errprintf("xstrncpy: Potential overwrite of %d bytes\n", (copyend - allocend));
		abort();
	}

	if (strlen(src) >= maxlen) {
		errprintf("xstrncpy: destination is not NULL terminated - src '%s', max %d\n", dest, src, maxlen);
	}
#endif

	strncpy(dest, src, maxlen);
	return dest;
}

int xsprintf(char *dest, const char *fmt, ...)
{
	va_list args;
	size_t printedbytes;
#ifdef MEMORY_DEBUG
	size_t availablebytes;
#endif

	if (dest == NULL) {
		errprintf("xsprintf: NULL destination\n");
		abort();
	}

#ifdef MEMORY_DEBUG
	dmem = find_in_memlist(dest);
	if (dmem == NULL) {
		errprintf("xsprintf: Bogus destination\n");
		abort();
	}

	availablebytes = (dmem->sdata + dmem->ssize - dest);
	va_start(args, fmt);
	printedbytes = vsnprintf(dest, availablebytes, fmt, args);
	va_end(args);

	if (printedbytes >= availablebytes) {
		errprintf("xsprintf: Output was truncated\n");
		abort();
	}
#else
	va_start(args, fmt);
	printedbytes = vsprintf(dest, fmt, args);
	va_end(args);
#endif

	return printedbytes;
}

