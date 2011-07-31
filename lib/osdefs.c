/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/* Compatibility definitions for various OS's                                 */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

#include "osdefs.h"

#ifndef HAVE_SNPRINTF
int snprintf(char *str, size_t size, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	return vsprintf(str, format, args);
}
#endif

#ifndef HAVE_VSNPRINTF
int vsnprintf(char *str, size_t size, const char *format, va_list args)
{
	return vsprintf(str, format, args);
}
#endif

