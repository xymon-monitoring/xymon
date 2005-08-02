/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MATCHING_H__
#define __MATCHING_H__

/* The clients probably dont have the pcre headers */
#ifndef CLIENTONLY
#include <pcre.h>

extern pcre *compileregex(const char *pattern);
extern int timematch(char *tspec);
extern int namematch(char *needle, char *haystack, pcre *pcrecode);
#endif

#endif
