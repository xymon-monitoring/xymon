/*----------------------------------------------------------------------------*/
/* Hobbit                                                                     */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains memory management routines.                                    */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: memory.c,v 1.2 2005-01-15 22:15:05 henrik Exp $";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"

const char *xfreenullstr = "xfree: Trying to free a NULL pointer";

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

