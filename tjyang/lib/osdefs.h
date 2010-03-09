/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/* Compatibility definitions for various OS's                                 */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LIBBBGEN_OSDEFS_H__
#define __LIBBBGEN_OSDEFS_H__

#include "config.h"

#include <sys/types.h>
#include <stdarg.h>

#ifndef HAVE_SOCKLEN_T
typedef unsigned int socklen_t;
#endif


