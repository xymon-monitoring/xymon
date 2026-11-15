/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/server/xymond-rrd-rrdcached-last.c
 *
 * xymond-rrd-rrdcached-last FILE -- print the time of FILE's last update as
 * the file on disk holds it. Run with RRDCACHED_ADDRESS unset, so librrd
 * reads the file itself instead of asking rrdcached, which would flush it
 * first.
 * xymond-rrd-rrdcached-last -v -- print librrd's version.
 * Run by tests/server/xymond-rrd-rrdcached.sh.
 */

#include <stdio.h>
#include <string.h>
#include <rrd.h>

int main(int argc, char *argv[])
{
	time_t t;

	if (argc != 2) {
		fprintf(stderr, "usage: %s FILE | -v\n", argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "-v") == 0) {
		printf("%s\n", rrd_strversion());
		return 0;
	}
	t = rrd_last_r(argv[1]);
	if (t == -1) {
		fprintf(stderr, "%s: %s\n", argv[1], rrd_get_error());
		return 1;
	}
	printf("%ld\n", (long)t);
	return 0;
}
