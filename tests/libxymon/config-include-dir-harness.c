/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/libxymon/config-include-dir-harness.c
 *
 * An fopen() that refuses any file whose name contains "unopenable", linked
 * ahead of the C library's into a STANDALONE build of lib/stackio.c. It lets
 * the suite reach stackio's "cannot open" paths as root, which the CI lanes
 * run as: root opens a file whatever its permissions, and the portability
 * check refuses a chmod that relies on it not doing so. Any other name is
 * opened for reading, the only mode stackio asks for. Run by
 * tests/libxymon/config-include-dir.sh, which checks first that this fopen()
 * is the one the reader calls.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

FILE *fopen(const char *path, const char *mode)
{
	int fd;

	if (strstr(path, "unopenable")) { errno = EACCES; return NULL; }
	if (strcmp(mode, "r") != 0) { errno = EINVAL; return NULL; }

	fd = open(path, O_RDONLY);
	return (fd == -1) ? NULL : fdopen(fd, mode);
}
