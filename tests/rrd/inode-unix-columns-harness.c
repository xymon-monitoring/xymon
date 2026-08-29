/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <rrd.h>

int main(int argc, char **argv)
{
	time_t last_update;
	unsigned long ds_count, index;
	char **ds_names = NULL;
	char **last_values = NULL;
	int result;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s RRD\n", argv[0]);
		return 2;
	}

	result = rrd_lastupdate_r(argv[1], &last_update, &ds_count, &ds_names, &last_values);
	if (result != 0) {
		fprintf(stderr, "Cannot read %s: %s\n", argv[1], rrd_get_error());
		return 1;
	}

	for (index = 0; index < ds_count; index++)
		printf("%s%s", (index ? " " : ""), ds_names[index]);
	printf("\n%lld:", (long long)last_update);
	for (index = 0; index < ds_count; index++)
		printf(" %s", last_values[index]);
	printf("\n");

	for (index = 0; index < ds_count; index++) {
		rrd_freemem(ds_names[index]);
		rrd_freemem(last_values[index]);
	}
	rrd_freemem(ds_names);
	rrd_freemem(last_values);

	return 0;
}
