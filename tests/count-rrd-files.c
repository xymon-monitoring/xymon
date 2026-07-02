#include <stdio.h>

#include "libxymon.h"

static int failures = 0;

static void expect_count(const char *host, const char *graph, int want)
{
	int got = count_rrd_files_for_graph((char *)host, (char *)graph);

	if (got != want) {
		fprintf(stderr, "count_rrd_files_for_graph(%s, %s): got %d, want %d\n",
			host, graph, got, want);
		failures++;
	}
}

int main(void)
{
	const char *host = "testhost";
	const char *otherhost = "otherhost";

	expect_count(host, "disk", 2);
	expect_count(host, "smart-temp", 2);
	expect_count(host, "dirgraph", 1);
	expect_count(host, "commented", 1);
	expect_count(host, "tcp", 2);
	expect_count(host, "http", 1);
	expect_count(host, "ncv", 2);
	expect_count(host, "devmon", 2);
	expect_count(host, "bundle", 4);
	expect_count(host, "la", 1);
	expect_count(host, "vmstat1", 1);
	expect_count(host, "plain", 1);
	expect_count(host, "orphan", 1);
	expect_count(host, "missing", 0);

	expect_count(otherhost, "disk", 1);
	expect_count(otherhost, "tcp", 1);
	expect_count(otherhost, "la", 0);
	expect_count(otherhost, "orphan", 0);
	expect_count(host, "disk", 2);

	return (failures == 0) ? 0 : 1;
}
