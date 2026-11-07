/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __FILES_H__
#define __FILES_H__

#include <fcntl.h>

/* O_NOFOLLOW is POSIX.1-2008; writers use it to refuse a symlink planted at a
 * file they write. Where <fcntl.h> predates it, fall back to 0 so the tree
 * builds - the pre-hardening exposure, not a build break. */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

extern void dropdirectory(char *dirfn, int background);

#endif

