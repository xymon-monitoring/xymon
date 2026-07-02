#include <stdio.h>
#include <string.h>

#include "libxymon.h"

static int failures = 0;

static int count_occurrences(const char *haystack, const char *needle)
{
	int count = 0;
	const char *p = haystack;

	while ((p = strstr(p, needle)) != NULL) {
		count++;
		p += strlen(needle);
	}

	return count;
}

static void expect_contains(const char *label, const char *text, const char *needle)
{
	if (!strstr(text, needle)) {
		fprintf(stderr, "%s: missing '%s' in:\n%s\n", label, needle, text);
		failures++;
	}
}

static void expect_not_contains(const char *label, const char *text, const char *needle)
{
	if (strstr(text, needle)) {
		fprintf(stderr, "%s: unexpected '%s' in:\n%s\n", label, needle, text);
		failures++;
	}
}

static void expect_occurrences(const char *label, const char *text, const char *needle, int want)
{
	int got = count_occurrences(text, needle);

	if (got != want) {
		fprintf(stderr, "%s: '%s' occurred %d time(s), want %d in:\n%s\n",
			label, needle, got, want, text);
		failures++;
	}
}

int main(void)
{
	xymongraph_t tcpgraph = { "tcp", NULL, 3 };
	xymongraph_t ncvgraph = { "ncv", NULL, 0 };
	xymongraph_t devmongraph = { "devmon", NULL, 0 };
	xymongraph_t lagraph = { "la", NULL, 0 };
	char *text;

	text = xymon_graph_data("host1", "Host One", "smtp", -1, &tcpgraph, 1,
				HG_WITHOUT_STALE_RRDS, HG_PLAIN_LINK, 0, 100, 200);
	expect_contains("tcp service mapping", text, "service=tcp:smtp");
	expect_contains("tcp service mapping", text, "first=1");
	expect_contains("tcp service mapping", text, "count=1");
	expect_contains("tcp service mapping", text, "nostale");
	expect_contains("tcp service mapping", text, "graph_start=100");
	expect_contains("tcp service mapping", text, "graph_end=200");

	text = xymon_graph_data("host1", "Host One", "foo", -1, &ncvgraph, 1,
				HG_WITH_STALE_RRDS, HG_PLAIN_LINK, 0, 100, 200);
	expect_contains("ncv service mapping", text, "service=ncv:foo");
	expect_not_contains("ncv stale flag", text, "nostale");

	text = xymon_graph_data("host1", "Host One", "temp", -1, &devmongraph, 1,
				HG_WITHOUT_STALE_RRDS, HG_PLAIN_LINK, 0, 100, 200);
	expect_contains("devmon service mapping", text, "service=devmon:temp");

	text = xymon_graph_data("host1", "Host One", "cpu", -1, &lagraph, 0,
				HG_WITH_STALE_RRDS, HG_PLAIN_LINK, 0, 100, 200);
	expect_contains("plain graph mapping", text, "service=la");
	expect_not_contains("plain graph no partitioning", text, "first=");
	expect_not_contains("plain graph no partitioning", text, "count=");

	text = xymon_graph_data("host1", "Host One", NULL, -1, &tcpgraph, 7,
				HG_WITH_STALE_RRDS, HG_PLAIN_LINK, 0, 100, 200);
	expect_occurrences("multi-part graph split", text, "service=tcp", 12);
	expect_contains("multi-part graph split", text, "first=1");
	expect_contains("multi-part graph split", text, "first=3");
	expect_contains("multi-part graph split", text, "first=5");
	expect_contains("multi-part graph split", text, "first=7");
	expect_occurrences("multi-part graph split", text, "count=2", 12);

	return (failures == 0) ? 0 : 1;
}
