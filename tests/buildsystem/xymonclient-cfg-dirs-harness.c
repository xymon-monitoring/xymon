/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/buildsystem/xymonclient-cfg-dirs-harness.c
 *
 * Loads the configuration file it is given, as xymonlaunch does with --env,
 * and prints what a program started from it finds in its environment for
 * XYMONTMP and XYMONCLIENTLOGS. Linked with a lib/environ.c the test
 * compiles itself, with or without relocated client directories.
 */
#include <stdio.h>
#include <stdlib.h>

#include "libxymon.h"

int main(int argc, char *argv[])
{
	char *tmp, *logs;

	if (argc != 2) return 2;
	loadenv(argv[1], NULL);
	tmp = getenv("XYMONTMP");
	logs = getenv("XYMONCLIENTLOGS");
	printf("%s|%s\n", (tmp ? tmp : "(unset)"), (logs ? logs : "(unset)"));
	return 0;
}
