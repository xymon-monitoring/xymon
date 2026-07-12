/* Guard for issue #244's paging awareness: the disk-family graph paging
 * counts status lines, and a filesystem the generic RRDEXCLUDE/RRDINCLUDE filter
 * keeps out of the RRDs must not be counted - its slot would render as a
 * broken image. The counter converts each line's mount point to the RRD
 * instance name and asks the same rrd_is_filtered() the writer uses. */
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

	/* df report: header + 15 zdata filesystems + 5 poudriere ones */
	snprintf(dfmsg, sizeof(dfmsg), "Filesystem 1024-blocks Used Avail Capacity Mounted on\n");
	for (i = 1; i <= 15; i++)
		snprintf(dfmsg + strlen(dfmsg), sizeof(dfmsg) - strlen(dfmsg),
			 "zdata/fs%d 1000 500 500 50%% /zdata/fs%d\n", i, i);
	for (i = 1; i <= 5; i++)
		snprintf(dfmsg + strlen(dfmsg), sizeof(dfmsg) - strlen(dfmsg),
			 "zdata/p%d 1000 500 500 50%% /poudriere/data%d\n", i, i);

	/* The RRDEXCLUDE/RRDINCLUDE patterns are compiled once per process, so the
	 * wrapper runs this harness once per scenario; argv[1] picks the
	 * expectations matching the environment the wrapper set. */
	if (argc > 1) mode = argv[1];
	html = render("disk", dfmsg);

	if (strcmp(mode, "none") == 0) {
		/* No filters: all 20 lines count (header subtracted) */
		expect_contains(mode, html, "<!-- linecount=20 -->");
		expect_contains(mode, html, "first=16&amp;count=5");
		expect_not_contains(mode, html, "first=21");
	}
	else if (strcmp(mode, "excl") == 0) {
		/* RRDEXCLUDE=disk:poudriere - 5 instances uncounted -> 15 */
		expect_contains(mode, html, "<!-- linecount=15 -->");
		expect_contains(mode, html, "first=11&amp;count=5");
		expect_not_contains(mode, html, "first=16");
	}
	else if (strcmp(mode, "incl") == 0) {
		/* RRDINCLUDE=disk:^disk,zdata,fs - only matching instances -> 15 */
		expect_contains(mode, html, "<!-- linecount=15 -->");
		expect_not_contains(mode, html, "first=16");
	}
	else if (strcmp(mode, "both") == 0) {
		/* RRDINCLUDE matches everything, RRDEXCLUDE drops one: RRDEXCLUDE wins -> 19.
		 * 19 lines page as groups of 4 (rebalanced), firsts 1,5,9,13,17 */
		expect_contains(mode, html, "<!-- linecount=19 -->");
		expect_contains(mode, html, "first=17&amp;count=4");
		expect_not_contains(mode, html, "first=21");
	}
	else if (strcmp(mode, "root") == 0) {
		/* The "/" mount maps to instance "<test>,root" - the one special
		 * case in the filename conversion. Own tiny table. */
		char rootmsg[512];

		snprintf(rootmsg, sizeof(rootmsg),
			 "Filesystem 1024-blocks Used Avail Capacity Mounted on\n"
			 "/dev/da0 100 50 50 50%% /\n"
			 "zdata/a 100 50 50 50%% /zdata/a\n");
		free(html);
		html = render("disk", rootmsg);
		expect_contains(mode, html, "<!-- linecount=1 -->");
	}
	else {
		fprintf(stderr, "unknown mode '%s'\n", mode);
		failures++;
	}
	free(html);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
