/* Self-describing statuses: XYMON GRAPH markers in a status message declare
 * the graphs its page shows, each with its own paging count - derived from
 * the message's METRICS block, overridden by instances=N, or instances=all for an
 * unsliced render. Legacy DEVMON RRD banners imply store+show. */
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

static void expect_count(const char *label, const char *text, const char *needle, int wanted)
{
	int n = 0;
	const char *p = text;

	while (p && (p = strstr(p, needle)) != NULL) { n++; p += strlen(needle); }
	if (n != wanted) {
		fprintf(stderr, "%s: found '%s' %d times, wanted %d\n", label, needle, n, wanted);
		failures++;
	}
}

static char *render_log_msg(const char *service, int is_history, const char *flags, const char *restofmsg)
{
	char *html = NULL;
	size_t htmlsz = 0;
	FILE *out;
	char *msgcopy = strdup(restofmsg);	/* the marker parser may read a mutable buffer */

	out = open_memstream(&html, &htmlsz);
	if (!out) { perror("open_memstream"); exit(2); }

	generate_html_log("testhost", "Test Host", (char *)service, "127.0.0.1",
			  COL_GREEN, 0, "tester", (char *)flags,
			  0, "0 minutes", "green status ok", msgcopy,
			  NULL, 0, NULL, NULL, 0, NULL,
			  is_history, 1, 0, 0, NULL, NULL,
			  NULL, NULL, NULL, 3600, out);

	fclose(out);
	free(msgcopy);
	return html;
}

static const char *diskio_msg =
	"<!--XYMON METRICS: diskio_ops\n"
	"DS:reads:GAUGE:600:0:U DS:writes:GAUGE:600:0:U\n"
	/* Unknown declaration line with two fields: an ALL-CAPS keyword
	 * ending in ':' is never an instance, so the count stays 3. And an
	 * instance line with fewer values than declared DSes creates no
	 * file, so it must not count either. */
	"THRESHOLD:reads >90\n"
	"shortline 5\n"
	"ada0 10:20\n"
	"ada1 5:6\n"
	"da0 118:302\n"
	"-->\n"
	"<!--XYMON METRICS: diskio_busy\n"
	"DS:busy:GAUGE:600:0:100\n"
	"ada0 5\n"
	"ada1 10\n"
	"da0 42\n"
	"-->\n"
	"<!--XYMON METRICS: diskio_hidden\n"
	"DS:v:GAUGE:600:0:U\n"
	"x 1\n"
	"-->\n"
	"<!--XYMON GRAPH: diskio_ops -->\n"
	"<!--XYMON GRAPH: diskio_busy -->\n"
	"<!--XYMON GRAPH: diskio_sum instances=all -->\n"
	"<!--XYMON GRAPH: diskio_lat instances=10 -->\n"
	"<!--XYMON GRAPH: diskio_split instances=4 -->\n"
	"\n"
	"Disk I/O Status\n"
	"ada0: 10 r/s, 20 w/s\n";

int main(void)
{
	char *html;

	histlocation = HIST_NONE;

	/* A marker-only column: no TEST2RRD, no GRAPHS_diskio - everything
	 * (graph list and paging counts) comes from the message itself. */
	html = render_log_msg("diskio", 0, "", diskio_msg);
	expect_contains("marker page has a graph section", html, "<a name=\"begingraph\">");
	/* count derived from the METRICS block: 3 instances; the gdef's own
	 * MAXINSTANCESPERIMAGE 1 (graphs.cfg) slices them one per image */
	expect_contains("derived count with gdef MAXINSTANCESPERIMAGE", html, "service=diskio_ops&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=1");
	expect_contains("derived count with gdef MAXINSTANCESPERIMAGE", html, "service=diskio_ops&amp;graph_width=576&amp;graph_height=120&amp;first=3&amp;count=1");
	/* MAXINSTANCESPERIMAGE in the graph definition beats a legacy ::N in GRAPHS:
	 * diskio_split is listed as ::4 but its gdef says MAXINSTANCESPERIMAGE 2 */
	expect_contains("MAXINSTANCESPERIMAGE beats legacy ::N", html, "service=diskio_split&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	expect_contains("MAXINSTANCESPERIMAGE beats legacy ::N", html, "service=diskio_split&amp;graph_width=576&amp;graph_height=120&amp;first=3&amp;count=2");
	/* GRAPHS lists diskio_busy::2; 3 instances at cap 2 fill 2 images of
	 * step 2 - the step rounds up to fill images, the last one holds the
	 * remainder */
	expect_contains("::N split applies", html, "service=diskio_busy&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	expect_contains("::N split applies", html, "service=diskio_busy&amp;graph_width=576&amp;graph_height=120&amp;first=3&amp;count=2");
	/* instances=all renders unsliced: no first/count in the URL */
	expect_contains("instances=all renders unsliced", html, "service=diskio_sum&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_not_contains("instances=all renders unsliced", html, "service=diskio_sum&amp;graph_width=576&amp;graph_height=120&amp;first=");
	/* explicit count=10 pages on the default base: 2 slices of 5 */
	expect_contains("explicit count", html, "service=diskio_lat&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=5");
	expect_contains("explicit count", html, "service=diskio_lat&amp;graph_width=576&amp;graph_height=120&amp;first=6&amp;count=5");
	/* store-only block: no GRAPH marker, no link */
	expect_not_contains("METRICS without GRAPH is not shown", html, "service=diskio_hidden");
	free(html);

	/* The legacy devmon banner is store+show combined, count from its block. */
	html = render_log_msg("devtest", 0, "",
		"<!--DEVMON RRD: if_load 0 0\n"
		"DS:ds0:COUNTER:600:0:U DS:ds1:COUNTER:600:0:U\n"
		"eth0.0 4678222:9966777\n"
		"eth1.0 123:456\n"
		"-->\n"
		"status text\n");
	expect_contains("legacy DEVMON banner renders its graph", html, "service=if_load&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	free(html);

	/* A marker naming a graph GRAPHS_<service> already lists renders once. */
	html = render_log_msg("smart", 0, "",
		"<!--XYMON GRAPH: smart-temp -->\n"
		"<!--XYMON GRAPH: smart-extra -->\n"
		"status text\n");
	/* One rendered graph = 3 URL occurrences (href, img src, zoom link) */
	expect_count("marker deduped against GRAPHS_ list", html, "service=smart-temp&amp;", 3);
	expect_contains("non-listed marker still renders", html, "service=smart-extra&amp;");
	free(html);

	/* A marker naming the column's default graph renders once; other
	 * markers add graphs next to the default one. */
	html = render_log_msg("cpu", 0, "",
		"<!--XYMON GRAPH: la -->\n"
		"<!--XYMON GRAPH: extra_cpu -->\n"
		"status text\n");
	expect_count("marker deduped against default graph", html, "service=la&amp;", 3);
	expect_contains("marker adds a graph beside the default", html, "service=extra_cpu&amp;");
	free(html);

	/* An UNNAMED METRICS block + an UNNAMED GRAPH both default to the test
	 * name (name-optional). The block is ALSO rendered in place as a status
	 * table whose value cells are coloured by their declared THRESHOLDs. */
	html = render_log_msg("smart", 0, "",
		"<!--XYMON METRICS\n"
		"DS:temp:GAUGE:600:0:U:Celsius\n"
		"THRESHOLD:temp:>55:warn\n"
		"THRESHOLD:temp:>65:crit\n"
		"sda 38\n"
		"nvme0 29\n"
		"sdb 58\n"
		"hot 72\n"
		"-->\n"
		"<!--XYMON GRAPH -->\n"
		"status text\n");
	expect_contains("unnamed METRICS+GRAPH default to the test name", html, "service=smart&amp;");
	expect_contains("METRICS block renders a status table", html, "<table");
	expect_contains("table header is the DS name", html, ">temp</th>");
	expect_contains("table shows the raw value", html, "38</td>");
	expect_contains("warn-band value coloured yellow (58>55)", html, "alt=\"yellow\"");
	expect_contains("crit-band value coloured red (72>65)", html, "alt=\"red\"");
	free(html);

	/* Frameless mode (for the group detail page): the same status renders its
	 * CONTENT (body/table + graphs) but no page header/footer frame. */
	{
		char *full, *bare;
		const char *m =
			"<!--XYMON METRICS\nDS:temp:GAUGE:600:0:U\n"
			"THRESHOLD:temp:>30:crit\nsda 38\n-->\n"
			"<!--XYMON GRAPH -->\nstatus text\n";
		full = render_log_msg("smart", 0, "", m);
		htmllog_frameless = 1;
		bare = render_log_msg("smart", 0, "", m);
		htmllog_frameless = 0;
		expect_contains("frameless keeps the table", bare, "<table");
		expect_contains("frameless keeps the coloured cell", bare, "alt=\"red\"");
		expect_contains("frameless keeps the graph", bare, "service=smart&amp;");
		if (strlen(bare) >= strlen(full)) {
			fprintf(stderr, "frameless drops the frame: expected shorter, bare=%lu full=%lu\n",
				(unsigned long)strlen(bare), (unsigned long)strlen(full));
			failures++;
		}
		free(full); free(bare);
	}

	/* Marker names are exact identities: "diskio_busy2" must not inherit
	 * the ::2 split of the GRAPHS entry "diskio_busy" it prefix-matches -
	 * it pages on the default base (3 instances -> one slice of 3). */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_busy2\n"
		"DS:busy:GAUGE:600:0:100\n"
		"a 1\nb 2\nc 3\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_busy2 -->\n"
		"status text\n");
	expect_contains("prefix-matched GRAPHS entry lends no split size", html,
		"service=diskio_busy2&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=3");
	free(html);

	/* Only lines the writer will actually store count, and the count
	 * mirrors the writer's right-split: the value token is the LAST
	 * whitespace field, so "disk one 50" is the instance "disk one"
	 * (spaces allowed - a folder/mount name) with value 50, and DOES
	 * count. "lonely" has no value token and is written by neither, so
	 * it does not. 3 instances -> count 3. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_mixed\n"
		"DS:v:GAUGE:600:0:U\n"
		"ada0 10\n"
		"disk one 50\n"
		"lonely\n"
		"ada1 20\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_mixed -->\n"
		"status text\n");
	expect_contains("count matches what the writer writes", html, "service=diskio_mixed&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=3");
	free(html);

	/* GRAPH attributes are writer-style words - blank-delimited, matched
	 * whole: "note_instances=4" is not an instances= attribute, and
	 * neither "instances=allergic" (not =all) nor "instances=2x" (junk
	 * after the number) is valid. The first two page on their block's
	 * derived count of 2; the block-less third renders unsliced. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_tok\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\n"
		"b 2\n"
		"-->\n"
		"<!--XYMON METRICS: diskio_allg\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\n"
		"b 2\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_tok note_instances=4 -->\n"
		"<!--XYMON GRAPH: diskio_allg instances=allergic -->\n"
		"<!--XYMON GRAPH: diskio_numx instances=2x -->\n"
		"status text\n");
	expect_contains("note_instances=4 is not instances=4", html,
		"service=diskio_tok&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	expect_not_contains("note_instances=4 is not instances=4", html, "count=4");
	expect_contains("instances=allergic is not instances=all", html,
		"service=diskio_allg&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	expect_not_contains("instances=allergic is not instances=all", html,
		"service=diskio_allg&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_contains("instances=2x is malformed and ignored", html,
		"service=diskio_numx&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_not_contains("instances=2x is malformed and ignored", html,
		"service=diskio_numx&amp;graph_width=576&amp;graph_height=120&amp;first=");
	free(html);

	/* The block writer switches blocks on EVERY devmon banner - it
	 * accepts any name - so a banner whose name the parser rejects must
	 * still close the open block: the count stays 2 (a, b), instead of
	 * the next block's lines inflating it to 5. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_cut\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\n"
		"b 2\n"
		"<!--DEVMON RRD: foo:bar 0 0\n"
		"DS:v:GAUGE:600:0:U\n"
		"c 3\n"
		"d 4\n"
		"e 5\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_cut -->\n"
		"status text\n");
	expect_contains("invalid devmon banner still closes the open block", html,
		"service=diskio_cut&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	free(html);

	/* The writer reads at most MAXCOLS (20) columns per line, so a 21st
	 * DS spec never becomes a dataset - instance lines carrying the 20
	 * values the writer stores must count: 2, not 0 (unsliced). */
	{
		char widemsg[2048];
		int n, i;

		n = snprintf(widemsg, sizeof(widemsg), "<!--XYMON METRICS: diskio_wide\n");
		for (i = 0; (i < 21); i++)
			n += snprintf(widemsg+n, sizeof(widemsg)-n, "%sDS:d%d:GAUGE:600:0:U", (i ? " " : ""), i);
		snprintf(widemsg+n, sizeof(widemsg)-n,
			"\nw0 1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1"
			"\nw1 1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1"
			"\n-->\n"
			"<!--XYMON GRAPH: diskio_wide -->\n"
			"status text\n");
		html = render_log_msg("diskio", 0, "", widemsg);
		expect_contains("DS count capped at the writer's MAXCOLS", html,
			"service=diskio_wide&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
		free(html);
	}

	/* An unknown banner attribute is ignored: the block's own instance
	 * lines drive the paging count. Explicit instances= wins. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_lazy futureattr\n"
		"DS:r:GAUGE:600:0:U DS:w:GAUGE:600:0:U\n"
		"a 0:0\n"
		"b 3:0\n"
		"c 0:0\n"
		"d 0:7\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_lazy -->\n"
		"<!--XYMON GRAPH: diskio_lazysliced instances=6 -->\n"
		"<!--XYMON METRICS: diskio_lazysliced\n"
		"DS:v:GAUGE:600:0:U\n"
		"x 1\n"
		"-->\n"
		"status text\n");
	expect_contains("unknown banner attribute ignored: block instances drive the count", html, "service=diskio_lazy&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=4");
	expect_contains("explicit count= still slices", html, "service=diskio_lazysliced&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=3");
	expect_contains("explicit count= still slices", html, "service=diskio_lazysliced&amp;graph_width=576&amp;graph_height=120&amp;first=4&amp;count=3");
	free(html);

	/* A store-filtered graph's file set diverges from the message, and
	 * the writer-kept fileset index knows it exactly: [diskio_idx] has
	 * STOREPATTERN and the harness index holds 3 fresh file entries plus
	 * one stale: 3, staleness cut applied. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_idx\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_idx -->\n"
		"status text\n");
	expect_contains("store-filtered count from the fileset index", html,
		"service=diskio_idx&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=3");
	free(html);

	/* FNPATTERN-derived counting: [diskio_gzy] is store-filtered with
	 * FNPATTERN ^gzyfiles, and the harness index holds two fresh
	 * gzyfiles entries, so the count is pattern-derived (2), not
	 * prefix- or message-derived. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_gzy\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\nb 2\nc 3\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_gzy -->\n"
		"status text\n");
	expect_contains("store-filter count is FNPATTERN-derived from the index", html,
		"service=diskio_gzy&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	free(html);

	/* A store-filtered graph (EXSTOREPATTERN/STOREPATTERN) with no index
	 * entries matching it still renders unsliced: its file set diverges
	 * from the message and nothing else knows it. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_filt\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\nb 2\nx 3\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_filt -->\n"
		"status text\n");
	expect_contains("store-filtered graphs render unsliced", html, "service=diskio_filt&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_not_contains("store-filtered graphs render unsliced", html, "service=diskio_filt&amp;graph_width=576&amp;graph_height=120&amp;first=");
	free(html);

	/* EXSTALEPATTERN exempts matching instances from the staleness
	 * window: the same index entries that count 3 for [diskio_idx]
	 * count 4 for [diskio_slow], whose pattern matches the stale
	 * "diskio_idx.old.rrd" entry - exempt instances always count. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_slow\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_slow -->\n"
		"status text\n");
	expect_contains("EXSTALEPATTERN-exempt instances always count", html,
		"service=diskio_slow&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=4");
	free(html);

	/* The GRAPHS_<service> config path takes the same index count: a
	 * GRAPHS-listed store-filtered gdef pages on the pattern-derived
	 * fileset. */
	html = render_log_msg("gzycol", 0, "", "plain status text\n");
	expect_contains("GRAPHS-listed store-filtered gdef counts from the index", html,
		"service=diskio_gzy&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	free(html);

	/* A hostile count= must not drive the renderer into building a
	 * giant page: absurd values render unsliced. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON GRAPH: diskio_huge instances=2000000000 -->\n"
		"status text\n");
	expect_contains("absurd count renders unsliced", html, "service=diskio_huge&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_not_contains("absurd count renders unsliced", html, "service=diskio_huge&amp;graph_width=576&amp;graph_height=120&amp;first=");
	free(html);

	/* An unclosed METRICS block is malformed: its count is unknown, so
	 * the graph renders unsliced instead of slicing on the status text. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_broken\n"
		"DS:v:GAUGE:600:0:U\n"
		"a 1\n"
		"<!--XYMON GRAPH: diskio_broken -->\n"
		"line one of status text\n"
		"line two of status text\n");
	expect_contains("unclosed block renders unsliced", html, "service=diskio_broken&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_not_contains("unclosed block renders unsliced", html, "service=diskio_broken&amp;graph_width=576&amp;graph_height=120&amp;first=");
	free(html);

	/* A self-closed one-line banner is an empty block: the status text
	 * after it is body, not instances - count 0, unsliced render. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_selfclosed -->\n"
		"<!--XYMON GRAPH: diskio_selfclosed -->\n"
		"all ok\n"
		"more ok\n");
	expect_contains("self-closed banner counts nothing", html, "service=diskio_selfclosed&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_not_contains("self-closed banner counts nothing", html, "service=diskio_selfclosed&amp;graph_width=576&amp;graph_height=120&amp;first=");
	free(html);

	/* A block with no DS line writes no files, so nothing is counted. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON METRICS: diskio_nods\n"
		"a 1\n"
		"b 2\n"
		"-->\n"
		"<!--XYMON GRAPH: diskio_nods -->\n"
		"status text\n");
	expect_contains("block without DS counts nothing", html, "service=diskio_nods&amp;graph_width=576&amp;graph_height=120&amp;disp=");
	expect_not_contains("block without DS counts nothing", html, "service=diskio_nods&amp;graph_width=576&amp;graph_height=120&amp;first=");
	free(html);

	/* A devmon-MAPPED column (TEST2RRD if_load=devmon, and if_load is a
	 * default --multigraphs member so the banner scan runs) renders
	 * through the marker path only: the legacy service-level fallback
	 * would add a second, imprecise copy next to the banner's graph. */
	html = render_log_msg("if_load", 0, "",
		"<!--DEVMON RRD: if_load2 0 0\n"
		"DS:ds0:COUNTER:600:0:U\n"
		"eth0.0 1\n"
		"eth1.0 2\n"
		"-->\n"
		"status text\n");
	expect_count("devmon column: banner graph rendered once", html, "service=if_load2&amp;", 3);
	/* the legacy fallback link renders as service=devmon:if_load */
	expect_not_contains("devmon column: no legacy fallback duplicate", html, "service=devmon");
	free(html);

	/* The legacy writer splits the devmon banner with strtok(" ") - space
	 * only - so a tab after "DEVMON RRD:" is part of the basename. Such a
	 * name is unparseable here: no marker for it, and the legacy fallback
	 * must stay, or the block's graphs would be lost. */
	html = render_log_msg("if_load", 0, "",
		"<!--DEVMON RRD: \tfoo 0 0\n"
		"DS:ds0:COUNTER:600:0:U\n"
		"eth0.0 1\n"
		"-->\n"
		"status text\n");
	expect_not_contains("tab-named devmon banner yields no marker", html, "service=foo&amp;");
	expect_contains("tab-named devmon banner keeps the legacy fallback", html, "service=devmon");
	free(html);

	/* Legacy DEVMON block: an instance named like a declaration keyword
	 * is data - the METRICS-only contract must not skip it. */
	html = render_log_msg("devtest", 0, "",
		"<!--DEVMON RRD: if_load 0 0\n"
		"DS:ds0:COUNTER:600:0:U DS:ds1:COUNTER:600:0:U\n"
		"CPU:1 47:70\n"
		"eth0.0 1:2\n"
		"-->\n"
		"status text\n");
	expect_contains("legacy devmon keyword-named instance counted", html,
		"service=if_load&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=2");
	free(html);

	/* Invalid names are ignored; a page with only invalid markers has no
	 * graph section at all. */
	html = render_log_msg("diskio", 0, "",
		"<!--XYMON GRAPH: ../evil -->\n"
		"<!--XYMON GRAPH: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa -->\n"
		"status text\n");
	/* The raw body (including the marker text) is echoed into the page,
	 * so assert on the absence of links, not of the text itself. */
	expect_not_contains("invalid marker names ignored", html, "service=");
	expect_not_contains("invalid marker names ignored", html, "begingraph");
	free(html);

	/* Markers quoted mid-line are body text, not markers. */
	html = render_log_msg("diskio", 0, "",
		"the docs mention <!--XYMON GRAPH: quoted --> in passing\n");
	expect_not_contains("mid-line marker text ignored", html, "begingraph");
	free(html);

	/* Reverse tests collect no RRD data: no marker graphs either. */
	html = render_log_msg("diskio", 0, "oRdastle", diskio_msg);
	expect_not_contains("reverse test has no marker graphs", html, "begingraph");
	free(html);

	/* History pages never render graphs. */
	html = render_log_msg("diskio", 1, "", diskio_msg);
	expect_not_contains("history page has no marker graphs", html, "begingraph");
	free(html);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
