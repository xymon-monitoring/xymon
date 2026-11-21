/*
 * tests/web/alttag-long-name-harness.c -- for alttag-long-name.sh
 *
 * Prints the ALT text alttag() builds for a column name of argv[1] bytes,
 * coloured red, acknowledged and not propagating, and its length.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

int main(int argc, char **argv)
{
	static char name[2048];
	size_t n;
	char *tag;

	if (argc < 2) return 2;
	n = (size_t)atoi(argv[1]);
	if (n >= sizeof(name)) return 2;
	memset(name, 'c', n);
	name[n] = '\0';

	tag = alttag(name, COL_RED, 1, 0, "3 days 01:02:03");
	printf("%lu %s\n", (unsigned long)strlen(tag), tag);
	return 0;
}
