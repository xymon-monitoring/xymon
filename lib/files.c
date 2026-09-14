/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for file- and directory manipulation.                 */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#include "libxymon.h"

void dropdirectory(char *dirfn, int background)
{
	DIR *dirfd;
	struct dirent *de;
	char fn[PATH_MAX];
	struct stat st;
	pid_t childpid = 0;

	if (background) {
		/* Caller wants us to run as a background task. */
		childpid = fork();
	}

	MEMDEFINE(fn);

	if (childpid == 0) {
		dbgprintf("Starting to remove directory %s\n", dirfn);
		/*
		 * A symlink is never descended: opendir() would follow it and we would
		 * delete whatever it points at, outside the tree we were asked to
		 * remove (a planted "histlogs/host -> ../../elsewhere" turned a
		 * drophost into a recursive delete of elsewhere). Remove the link
		 * itself and stop.
		 */
		if ((lstat(dirfn, &st) == 0) && S_ISLNK(st.st_mode)) {
			unlink(dirfn);
		}
		else if ((dirfd = opendir(dirfn)) != NULL) {
			while ( (de = readdir(dirfd)) != NULL ) {
				/* A child name that does not fit is skipped, not acted on
				   truncated - a truncated path names a different entry. */
				if ((size_t)snprintf(fn, sizeof(fn), "%s/%s", dirfn, de->d_name) >= sizeof(fn)) continue;
				/* lstat(), not stat(): a symlinked child is unlinked as a link,
				   never followed; only a real subdirectory is recursed into. */
				if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..") && (lstat(fn, &st) == 0)) {
					if (S_ISDIR(st.st_mode)) {
						dbgprintf("Recurse into %s\n", fn);
						dropdirectory(fn, 0); /* Don't background the recursive calls! */
					}
					else {
						dbgprintf("Removing %s\n", fn);
						unlink(fn);
					}
				}
			}
			closedir(dirfd);
			dbgprintf("Removing directory %s\n", dirfn);
			rmdir(dirfn);
		}
		if (background) {
			/* Background task just exits */
			exit(0);
		}
	}
	else if (childpid < 0) {
		errprintf("Could not fork child to remove directory %s\n", dirfn);
	}

	MEMUNDEFINE(fn);
}

