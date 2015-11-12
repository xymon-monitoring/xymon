/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MATCHING_H__
#define __MATCHING_H__

/* The clients probably don't have the pcre headers */
#if defined(LOCALCLIENT) || !defined(CLIENTONLY)
#include <pcre.h>
#include <stdarg.h>

extern pcre *compileregex(const char *pattern);
extern pcre *compileregex_opts(const char *pattern, int flags);
#ifdef PCRE_FIRSTLINE
#define firstlineregex(P) compileregex_opts(P, PCRE_FIRSTLINE);
#define firstlineregexnocase(P) compileregex_opts(P, PCRE_CASELESS|PCRE_FIRSTLINE);
#else
#define firstlineregex(P) compileregex_opts(P, 0);
#define firstlineregexnocase(P) compileregex_opts(P, PCRE_CASELESS);
#endif
extern pcre *multilineregex(const char *pattern);
extern int matchregex(const char *needle, pcre *pcrecode);
extern void freeregex(pcre *pcrecode);
extern int namematch(const char *needle, char *haystack, pcre *pcrecode);
extern int patternmatch(char *datatosearch, char *pattern, pcre *pcrecode);
extern pcre **compile_exprs(char *id, const char **patterns, int count);
extern int pickdata(char *buf, pcre *expr, int dupok, ...);
extern int timematch(char *holidaykey, char *tspec);
#endif

#endif
