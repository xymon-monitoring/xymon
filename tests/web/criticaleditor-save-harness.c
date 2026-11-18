/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/web/criticaleditor-save-harness.c
 *
 * An fopen() linked ahead of the C library's into a build of
 * criticaleditor.cgi, for tests/web/criticaleditor-save.sh. It refuses to
 * open for reading a file whose name contains "unreadable", and to open for
 * writing one whose name contains "unwritable". That reaches the editor's
 * "cannot read" path as root, which the CI lanes run as: root opens a file
 * whatever its permissions, and the portability check refuses a chmod that
 * relies on it not doing so. The test checks first that this fopen() is the
 * one the editor calls. Any other open goes through open() and fdopen().
 *
 * With _FILE_OFFSET_BITS=64, glibc's stdio.h renames fopen() to fopen64(),
 * so the editor calls fopen64(): both are defined here. The setting is
 * dropped for this file, so that its own fopen() keeps its name.
 */

#undef _FILE_OFFSET_BITS
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

static FILE *refusing_fopen(const char *path, const char *mode)
{
	int flags, fd;

	switch (*mode) {
	  case 'r': flags = (strchr(mode, '+') ? O_RDWR : O_RDONLY); break;
	  case 'w': flags = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
	  case 'a': flags = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
	  default: errno = EINVAL; return NULL;
	}

	if ((*mode == 'r') && !strchr(mode, '+') && strstr(path, "unreadable")) { errno = EACCES; return NULL; }
	if ((*mode != 'r') && strstr(path, "unwritable")) { errno = EACCES; return NULL; }

	fd = open(path, flags, 0644);
	return (fd == -1) ? NULL : fdopen(fd, mode);
}

FILE *fopen(const char *path, const char *mode)
{
	return refusing_fopen(path, mode);
}

FILE *fopen64(const char *path, const char *mode);
FILE *fopen64(const char *path, const char *mode)
{
	return refusing_fopen(path, mode);
}
