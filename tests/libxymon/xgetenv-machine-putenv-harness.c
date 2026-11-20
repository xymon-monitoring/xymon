/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/libxymon/xgetenv-machine-putenv-harness.c
 *
 * Asks xgetenv() for MACHINE with only MACHINEDOTS set, and prints the
 * answer. Built by tests/libxymon/xgetenv-machine-putenv.sh against
 * lib/environ.c compiled as on a system without setenv().
 */

#include <stdio.h>
#include <stdlib.h>
#include "libxymon.h"

int main(void)
{
	char *v = xgetenv("MACHINE");

	printf("%s\n", v ? v : "(null)");
	return 0;
}
