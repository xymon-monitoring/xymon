#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static int failures = 0;

static void expect_contains(const char *label, const char *text, const char *needle)
{
	if (!strstr(text, needle)) {
		fprintf(stderr, "%s: missing '%s' in generated HTML\n", label, needle);
		failures++;
	}
}

static void expect_not_contains(const char *label, const char *text, const char *needle)
{
	if (strstr(text, needle)) {
		fprintf(stderr, "%s: unexpected '%s' in generated HTML\n", label, needle);
		failures++;
	}
}

static char *render_log(int is_history, int render_graphs)
{
	char *html = NULL;
	size_t htmlsz = 0;
	FILE *out;

	out = open_memstream(&html, &htmlsz);
	if (!out) {
		perror("open_memstream");
		exit(2);
	}

	generate_html_log("testhost", "Test Host", "smart", "127.0.0.1",
			  COL_GREEN, 0, "tester", "",
			  0, "0 minutes", "green smart ok", "smart status body",
			  NULL, 0, NULL, NULL, 0, NULL,
			  is_history, 1, 0, 0, NULL, NULL,
			  NULL, NULL, NULL, 3600, render_graphs, out);

	fclose(out);
	return html;
}

int main(void)
{
	char *html;

	histlocation = HIST_NONE;

	html = render_log(0, 1);
	expect_contains("manual custom graph", html, "<!-- GRAPHS_smart: smart-temp -->");
	expect_contains("manual custom graph", html, "<a name=\"begingraph\">");
	expect_contains("manual custom graph", html, "service=smart-temp");
	expect_contains("manual custom graph", html, "first=1");
	expect_contains("manual custom graph", html, "count=2");
	expect_contains("manual custom graph", html, "nostale");
	free(html);

	html = render_log(0, 0);
	expect_not_contains("render_graphs disabled", html, "GRAPHS_smart");
	expect_not_contains("render_graphs disabled", html, "service=smart-temp");
	free(html);

	html = render_log(1, 1);
	expect_not_contains("history graph suppression", html, "GRAPHS_smart");
	expect_not_contains("history graph suppression", html, "service=smart-temp");
	free(html);

	return (failures == 0) ? 0 : 1;
}
