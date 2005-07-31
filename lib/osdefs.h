/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/* Compatibility definitions for various OS's                                 */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LIBBBGEN_OSDEFS_H__
#define __LIBBBGEN_OSDEFS_H__

#include <sys/types.h>
#include <stdarg.h>

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#ifndef HAVE_SNPRINTF
extern int snprintf(char *str, size_t size, const char *format, ...);
#endif

#ifndef HAVE_VSNPRINTF
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
#endif

#endif

