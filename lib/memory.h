/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MEMORY_H__
#define __MEMORY_H__

#undef MEMORY_DEBUG

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


#ifndef LIB_MEMORY_C_COMPILE
#undef calloc
#undef malloc
#undef realloc
#undef strdup

/*
 * This arranges for all memory-allocation routines to
 * go via a wrapper that checks for bogus input data
 * and malloc() returning NULL when running out of memory.
 * Errors caught here are fatal.
 * Overhead is small, so this is turned on always.
 */
#define calloc(N,S)  xcalloc((N), (S))
#define malloc(N)    xmalloc((N))
#define realloc(P,S) xrealloc((P), (S))
#define strdup(P)    xstrdup((P))
#endif


#ifdef MEMORY_DEBUG

/*
 * This arranges for all calls to potentially memory-overwriting routines
 * to do strict allocation checks and overwrite checks. The performance
 * overhead is significant, so it should only be turned on in debugging
 * situations.
 */

#ifndef LIB_MEMORY_C_COMPILE
#define MEMDEFINE(P) { add_to_memlist((P), sizeof((P))); }
#define MEMUNDEFINE(P) { remove_from_memlist((P)); }

#define xfree(P) { remove_from_memlist((P)); free((P)); (P) = NULL; }

#undef strcat
#undef strncat
#undef strcpy
#undef strncpy
#undef sprintf

#define strcat(D,S) xstrcat((D), (S))
#define strncat(D,S,L) xstrncat((D), (S), (L))
#define strcpy(D,S) xstrcpy((D), (S))
#define strncpy(D,S,L) xstrncpy((D), (S), (L))
#define sprintf xsprintf
#endif

#else

/*
 * This defines an "xfree()" macro, which checks for freeing of
 * a NULL ptr and complains if that happens. It does not check if
 * the pointer is valid.
 * After being freed, the pointer is set to NULL to catch double-free's.
 */

#define xfree(P) { if ((P) == NULL) { errprintf(xfreenullstr); abort(); } free((P)); (P) = NULL; }

#define MEMDEFINE(P) do { } while (0);
#define MEMUNDEFINE(P) do { } while (0);

#endif


#endif

