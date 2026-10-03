/* Prints what xymon_rundir() (lib/environ.c) resolves the runtime directory
 * to, for tests/server/xymon-rundir.sh. The environment is the input: the
 * test sets XYMONRUNDIR and XYMONSERVERLOGS before running it.
 */
#include <stdio.h>

#include "libxymon.h"

int main(void)
{
	printf("%s\n", xymon_rundir());
	return 0;
}
