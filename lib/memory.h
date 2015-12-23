/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MEMORY_H__
#define __MEMORY_H__

typedef struct xmemory_t {
	char *sdata;
	size_t ssize;
	struct xmemory_t *next;
} xmemory_t;

extern const char *xfreenullstr;

extern void add_to_memlist(void *ptr, size_t memsize);
extern void remove_from_memlist(void *ptr);

extern void *xcalloc(size_t nmemb, size_t size);
extern void *xmalloc(size_t size);
extern void *xrealloc(void *ptr, size_t size);

extern char *xstrdup(const char *s);
extern char *xstrcat(char *dest, const char *src);
extern char *xstrcpy(char *dest, const char *src);

extern char *xstrncat(char *dest, const char *src, size_t maxlen);
extern char *xstrncpy(char *dest, const char *src, size_t maxlen);
extern int   xsprintf(char *dest, const char *fmt, ...);
extern char *xresultbuf(int maxsz);


/*
 * This defines an "xfree()" macro, which checks for freeing of
 * a NULL ptr and complains if that happens. It does not check if
 * the pointer is valid.
 * After being freed, the pointer is set to NULL to catch double-free's.
 */

#define xfree(P) { if ((P) == NULL) { errprintf(xfreenullstr); abort(); } free((P)); (P) = NULL; }

#endif

