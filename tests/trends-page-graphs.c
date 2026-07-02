#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"
#include "svcstatus-trends.h"

static int failures = 0;

static void expect_contains(const char *label, const char *text, const char *needle)
{
	if (!text || !strstr(text, needle)) {
		fprintf(stderr, "%s: missing '%s'\n", label, needle);
		failures++;
	}
}

static void expect_not_contains(const char *label, const char *text, const char *needle)
{
	if (text && strstr(text, needle)) {
		fprintf(stderr, "%s: unexpected '%s'\n", label, needle);
		failures++;
	}
}

static char *render_trends(const char *host)
{
	char *trends = generate_trends((char *)host, 100, 200);

	if (!trends) {
		fprintf(stderr, "%s: generate_trends returned NULL\n", host);
		failures++;
	}

	return trends;
}

int main(void)
{
	char *trends;

	if (load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn()) < 0) {
		fprintf(stderr, "Cannot load test hosts.cfg\n");
		return 2;
	}

	trends = render_trends("testhost");
	if (trends) {
		expect_contains("default trends include custom graph", trends, "service=smart-temp&amp;graph_width");
		expect_contains("default trends include custom graph count", trends, "service=smart-temp&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
		expect_contains("default trends include bundled tcp graph", trends, "service=tcp&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
		expect_contains("default trends include dotted tcp graph", trends, "service=tcp.smtp&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=1");
		expect_contains("default trends include primary dotted graph", trends, "service=tcp.http&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=1");
		expect_contains("default trends include plain graph", trends, "service=la&amp;graph_width=576&amp;graph_height=120");
		expect_contains("default trends preserve start time", trends, "graph_start=100");
		expect_contains("default trends preserve end time", trends, "graph_end=200");
		free(trends);
	}

	trends = render_trends("limitedhost");
	if (trends) {
		expect_contains("TRENDS allowlist includes smart graph", trends, "service=smart-temp&amp;graph_width");
		expect_not_contains("TRENDS allowlist excludes bundled tcp graph", trends, "service=tcp&amp;graph_width");
		expect_not_contains("TRENDS allowlist excludes dotted tcp graph", trends, "service=tcp.smtp&amp;graph_width");
		free(trends);
	}

	trends = render_trends("mappedhost");
	if (trends) {
		expect_contains("TRENDS mapping includes custom status graph", trends, "service=smart&amp;graph_width");
		expect_contains("TRENDS mapping keeps explicit rrd graph", trends, "service=smart-temp&amp;graph_width");
		expect_not_contains("TRENDS mapping excludes unmapped tcp graph", trends, "service=tcp&amp;graph_width");
		free(trends);
	}

	/*
	 * The bulk/wide-host buffer-growth cases that used to live here exercise
	 * the trends link buffer, which is PR #38's territory; they belong with
	 * PR #38's tests. This file covers PR #44's graph counting/mapping only.
	 */

	return (failures == 0) ? 0 : 1;
}
