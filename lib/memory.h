/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
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

// strlen|strcmp|strncmp|strcasecmp|strncasecmp|strstr|strchr|strrchr|strspn|strcspn|strtok|strftime|strerror

#define calloc(N,S) xcalloc((N), (S))
#define malloc(N) xmalloc((N))
#define realloc(P,S) xrealloc((P), (S))
#define xfree(P) { remove_from_memlist((P)); free((P)); (P) = NULL; }

#define strdup(P) xstrdup((P))
#define strcat(D,S) xstrcat((D), (S))
#define strncat(D,S,L) xstrncat((D), (S), (L))

#define strcpy(D,S) xstrcpy((D), (S))
#define strncpy(D,S,L) xstrncpy((D), (S), (L))
#define sprintf xsprintf
#endif

