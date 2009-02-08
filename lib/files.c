/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for file- and directory manipulation.                 */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: files.c,v 1.9 2006-07-20 16:06:41 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#include "libbbgen.h"

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
		dirfd = opendir(dirfn);
		if (dirfd) {
			while ( (de = readdir(dirfd)) != NULL ) {
				sprintf(fn, "%s/%s", dirfn, de->d_name);
				if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..") && (stat(fn, &st) == 0)) {
					if (S_ISREG(st.st_mode)) {
						dbgprintf("Removing file %s\n", fn);
						unlink(fn);
					}
					else if (S_ISDIR(st.st_mode)) {
						dbgprintf("Recurse into %s\n", fn);
						dropdirectory(fn, 0); /* Dont background the recursive calls! */
					}
				}
			}
			closedir(dirfd);
		}
		dbgprintf("Removing directory %s\n", dirfn);
		rmdir(dirfn);
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

