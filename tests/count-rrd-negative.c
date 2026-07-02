#include <stdio.h>
#include <string.h>

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

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s missing-graphs|invalid-pattern|missing-rrddir\n", argv[0]);
		return 2;
	}

	if (strcmp(argv[1], "missing-graphs") == 0) {
		expect_count("testhost", "plain", 1);
		expect_count("testhost", "disk", 0);
	}
	else if (strcmp(argv[1], "invalid-pattern") == 0) {
		expect_count("testhost", "badpattern", 0);
		expect_count("testhost", "goodpattern", 1);
	}
	else if (strcmp(argv[1], "missing-rrddir") == 0) {
		expect_count("missinghost", "plain", 0);
		expect_count("missinghost", "disk", 0);
	}
	else {
		fprintf(stderr, "unknown test mode: %s\n", argv[1]);
		return 2;
	}

	return (failures == 0) ? 0 : 1;
}
