/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for file- and directory manipulation.                 */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: files.c,v 1.1 2004-11-13 22:34:04 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>

#include "errormsg.h"
#include "files.h"

void dropdirectory(char *dirfn)
{
	DIR *dirfd;
	struct dirent *de;
	char fn[PATH_MAX];
	struct stat st;
	pid_t childpid;

	childpid = fork();
	if (childpid == 0) {
		dprintf("Starting to remove directory %s\n", dirfn);
		dirfd = opendir(dirfn);
		if (dirfd) {
			while ( (de = readdir(dirfd)) != NULL ) {
				sprintf(fn, "%s/%s", dirfn, de->d_name);
				if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..") && (stat(fn, &st) == 0)) {
					if (S_ISREG(st.st_mode)) {
						dprintf("Removing file %s\n", fn);
						unlink(fn);
					}
					else if (S_ISDIR(st.st_mode)) {
						dprintf("Recurse into %s\n", fn);
						dropdirectory(fn);
					}
				}
			}
			closedir(dirfd);
		}
		dprintf("Removing directory %s\n", dirfn);
		rmdir(dirfn);
	}
	else if (childpid < 0) {
		errprintf("Could not fork child to remove directory %s\n", dirfn);
	}
}

