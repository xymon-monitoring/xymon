#include <stdio.h>

#include "libxymon.h"

static int failures = 0;

static void expect_count(const char *host, const char *graph, const char *svcfilter, int want)
{
	int got = count_rrd_files_for_graph((char *)host, (char *)graph, (char *)svcfilter);

	if (got != want) {
		fprintf(stderr, "count_rrd_files_for_graph(%s, %s, %s): got %d, want %d\n",
			host, graph, svcfilter ? svcfilter : "NULL", got, want);
		failures++;
	}
}

int main(void)
{
	const char *host = "testhost";
	const char *otherhost = "otherhost";

	expect_count(host, "disk", NULL, 2);
	expect_count(host, "smart-temp", NULL, 2);
	expect_count(host, "dirgraph", NULL, 1);
	expect_count(host, "commented", NULL, 1);
	expect_count(host, "tcp", NULL, 2);
	expect_count(host, "tcp", "smtp", 1);
	expect_count(host, "tcp", "ssh", 1);
	expect_count(host, "tcp", "pop3", 0);
	expect_count(host, "tcp.smtp", NULL, 1);
	expect_count(host, "tcp.pop3", NULL, 0);
	expect_count(host, "tcp.http", NULL, 1);
	expect_count(host, "http", NULL, 1);
	expect_count(host, "ncv", NULL, 2);
	expect_count(host, "ncv", "foo", 1);
	expect_count(host, "ncv.foo", NULL, 1);
	expect_count(host, "devmon", NULL, 2);
	expect_count(host, "devmon", "temp", 1);
	expect_count(host, "devmon.temp", NULL, 1);
	expect_count(host, "bundle", NULL, 4);
	expect_count(host, "bundle.smtp", NULL, 1);
	expect_count(host, "bundle.imap", NULL, 0);
	expect_count(host, "bundle.primary-match", NULL, 1);
	expect_count(host, "bundle.primary-def", NULL, 1);
	expect_count(host, "la", NULL, 1);
	expect_count(host, "vmstat1", NULL, 1);
	expect_count(host, "plain", NULL, 1);
	expect_count(host, "orphan", NULL, 1);
	expect_count(host, "missing", NULL, 0);

	expect_count(otherhost, "disk", NULL, 1);
	expect_count(otherhost, "tcp", NULL, 1);
	expect_count(otherhost, "tcp", "smtp", 1);
	expect_count(otherhost, "tcp.smtp", NULL, 1);
	expect_count(otherhost, "tcp", "ssh", 0);
	expect_count(otherhost, "la", NULL, 0);
	expect_count(otherhost, "orphan", NULL, 0);
	expect_count(host, "disk", NULL, 2);

	return (failures == 0) ? 0 : 1;
}
