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
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdarg.h>

extern pcre2_code *compileregex(const char *pattern);
extern pcre2_code *compileregex_opts(const char *pattern, uint32_t flags);
#ifdef PCRE_FIRSTLINE
#define firstlineregex(P) compileregex_opts(P, PCRE_FIRSTLINE);
#define firstlineregexnocase(P) compileregex_opts(P, PCRE_CASELESS|PCRE_FIRSTLINE);
#else
#define firstlineregex(P) compileregex_opts(P, 0);
#define firstlineregexnocase(P) compileregex_opts(P, PCRE_CASELESS);
#endif
extern pcre2_code *multilineregex(const char *pattern);
extern int matchregex(const char *needle, pcre2_code *pcrecode);
extern void freeregex(pcre2_code *pcrecode);
extern int namematch(const char *needle, char *haystack, pcre2_code *pcrecode);
extern int patternmatch(char *datatosearch, char *pattern, pcre2_code *pcrecode);
extern pcre2_code **compile_exprs(char *id, const char **patterns, int count);
extern int pickdata(char *buf, pcre2_code *expr, int dupok, ...);
extern int timematch(char *holidaykey, char *tspec);
#endif

#endif
