/* Regression test for issue #234: the graph paging on a disk-family status
 * page sized itself by counting status-text lines, ignoring that
 * NORRDDISKS/RRDDISKS hold some of those filesystems out of the RRDs - so
 * the page requested graph slices with no files behind them (dead images).
 * htmllog must mirror the do_disk filter when counting: same patterns, same
 * subject (the mount point, the line's last field), same precedence. */
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
	int i;

	histlocation = HIST_NONE;

	/* df report: header + 15 kept filesystems + 5 poudriere ones that
	 * NORRDDISKS (set by the wrapper script) holds out of the RRDs. */
	snprintf(dfmsg, sizeof(dfmsg), "Filesystem 1024-blocks Used Avail Capacity Mounted on\n");
	for (i = 1; i <= 15; i++)
		snprintf(dfmsg + strlen(dfmsg), sizeof(dfmsg) - strlen(dfmsg),
			 "zdata/fs%d 1000 500 500 50%% /zdata/fs%d\n", i, i);
	for (i = 1; i <= 5; i++)
		snprintf(dfmsg + strlen(dfmsg), sizeof(dfmsg) - strlen(dfmsg),
			 "zdata/p%d 1000 500 500 50%% /poudriere/data%d\n", i, i);

	/* The RRDDISKS/NORRDDISKS patterns are compiled once per process, so the
	 * wrapper runs this harness once per scenario; argv[1] picks the
	 * expectations matching the environment the wrapper set. */
	{
		const char *mode = "none";
		char lc[64];

		if (argc > 1) mode = argv[1];
		html = render("disk", dfmsg);

		if (strcmp(mode, "none") == 0) {
			/* No filters: all 20 lines count (header subtracted) */
			expect_contains(mode, html, "<!-- linecount=20 -->");
			expect_contains(mode, html, "first=16&amp;count=5");
			expect_not_contains(mode, html, "first=21");
		}
		else if (strcmp(mode, "excl") == 0) {
			/* NORRDDISKS=^/poudriere: 5 lines uncounted -> 15 */
			expect_contains(mode, html, "<!-- linecount=15 -->");
			expect_contains(mode, html, "first=11&amp;count=5");
			expect_not_contains(mode, html, "first=16");
		}
		else if (strcmp(mode, "incl") == 0) {
			/* RRDDISKS=^/zdata/fs: only matching lines count -> 15 */
			expect_contains(mode, html, "<!-- linecount=15 -->");
			expect_not_contains(mode, html, "first=16");
		}
		else if (strcmp(mode, "both") == 0) {
			/* include ^/ (everything), exclude one: exclude wins -> 19.
			 * 19 lines page as 4 groups of 4 (rebalanced), firsts 1,5,9,13,17 */
			expect_contains(mode, html, "<!-- linecount=19 -->");
			expect_contains(mode, html, "first=17&amp;count=4");
			expect_not_contains(mode, html, "first=21");
		}
		else {
			fprintf(stderr, "unknown mode '%s'\n", mode);
			failures++;
		}
		(void)lc;
		free(html);
	}

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
