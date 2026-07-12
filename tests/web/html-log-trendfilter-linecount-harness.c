/* Guard for issue #244's status-page graph paging: RRDEXCLUDE/RRDINCLUDE
 * must size graph links from the filtered RRD files, not from raw status
 * lines. This keeps the count generic instead of parsing one status format. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

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

static char *render(const char *service, const char *restofmsg)
{
	char *html = NULL;
	size_t htmlsz = 0;
	FILE *out;

	out = open_memstream(&html, &htmlsz);
	if (!out) { perror("open_memstream"); exit(2); }

	generate_html_log("testhost", "Test Host", (char *)service, "127.0.0.1",
			  COL_GREEN, 0, "tester", "",
			  0, "0 minutes", "green status ok", (char *)restofmsg,
			  NULL, 0, NULL, NULL, 0, NULL,
			  0, 1, 0, 0, NULL, NULL,
			  NULL, NULL, NULL, 3600, out);

	fclose(out);
	return html;
}

int main(int argc, char **argv)
{
	char dfmsg[4096];
	char *html;
	const char *mode = "none";
	int i;

	histlocation = HIST_NONE;

	snprintf(dfmsg, sizeof(dfmsg), "Filesystem 1024-blocks Used Avail Capacity Mounted on\n");
	for (i = 1; i <= 20; i++) {
		snprintf(dfmsg + strlen(dfmsg), sizeof(dfmsg) - strlen(dfmsg),
			 "zdata/fs%d 1000 500 500 50%% /reported/fs%d\n", i, i);
	}

	if (argc > 1) mode = argv[1];
	html = render("disk", dfmsg);

	if (strcmp(mode, "none") == 0) {
		/* 20 status rows, but only 8 matching RRD files exist. */
		expect_contains(mode, html, "<!-- linecount=8 -->");
		expect_contains(mode, html, "first=1&amp;count=4");
		expect_contains(mode, html, "first=5&amp;count=4");
		expect_not_contains(mode, html, "first=9");
	}
	else if (strcmp(mode, "excl") == 0) {
		/* RRDEXCLUDE removes 2 existing RRDs before paging is sized. */
		expect_contains(mode, html, "<!-- linecount=6 -->");
		expect_contains(mode, html, "first=1&amp;count=3");
		expect_contains(mode, html, "first=4&amp;count=3");
		expect_not_contains(mode, html, "first=7");
	}
	else if (strcmp(mode, "incl") == 0) {
		/* RRDINCLUDE keeps only the 5 zdata files. */
		expect_contains(mode, html, "<!-- linecount=5 -->");
		expect_contains(mode, html, "first=1&amp;count=5");
		expect_not_contains(mode, html, "first=6");
	}
	else if (strcmp(mode, "root") == 0) {
		/* The root RRD is filtered by filename, without parsing the status row. */
		expect_contains(mode, html, "<!-- linecount=7 -->");
		expect_contains(mode, html, "first=7&amp;count=3");
	}
	else {
		fprintf(stderr, "unknown mode '%s'\n", mode);
		failures++;
	}
	free(html);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
