/* Regression test for issue #31: GRAPHS_<service> custom graphs must render
 * on a status page even when the service has no default graph definition. */
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

static char *render_log_msg(const char *service, int is_history, const char *flags, const char *restofmsg)
{
	char *html = NULL;
	size_t htmlsz = 0;
	FILE *out;

	out = open_memstream(&html, &htmlsz);
	if (!out) { perror("open_memstream"); exit(2); }

	generate_html_log("testhost", "Test Host", (char *)service, "127.0.0.1",
			  COL_GREEN, 0, "tester", (char *)flags,
			  0, "0 minutes", "green status ok", (char *)restofmsg,
			  NULL, 0, NULL, NULL, 0, NULL,
			  is_history, 1, 0, 0, NULL, NULL,
			  NULL, NULL, NULL, 3600, out);

	fclose(out);
	return html;
}

static char *render_log(const char *service, int is_history)
{
	return render_log_msg(service, is_history, "", "status body");
}

int main(void)
{
	char *html;

	histlocation = HIST_NONE;

	/* The #31 case: "smart" has no default graph (not in TEST2RRD/GRAPHS),
	 * only GRAPHS_smart. Before the fix, no graph section rendered at all. */
	html = render_log("smart", 0);
	expect_contains("custom graph without default", html, "<!-- GRAPHS_smart: smart-temp,smart-status -->");
	expect_contains("custom graph without default", html, "<a name=\"begingraph\">");
	expect_contains("custom graph without default", html, "service=smart-temp");
	expect_contains("custom graph without default", html, "service=smart-status");
	free(html);

	/* The environment must not be mutated by tokenizing (legacy strtok bug):
	 * a second render of the same page must still see the full list. */
	html = render_log("smart", 0);
	expect_contains("environment not mutated", html, "<!-- GRAPHS_smart: smart-temp,smart-status -->");
	expect_contains("environment not mutated", html, "service=smart-status");
	free(html);

	/* Default path unchanged: cpu maps via TEST2RRD to la, no GRAPHS_cpu -
	 * the legacy link uses the mapped RRD name. */
	html = render_log("cpu", 0);
	expect_contains("default graph unchanged", html, "<a name=\"begingraph\">");
	expect_contains("default graph unchanged", html, "service=la");
	free(html);

	/* History pages never render graphs - unchanged. */
	html = render_log("smart", 1);
	expect_not_contains("history page has no graphs", html, "begingraph");
	expect_not_contains("history page has no graphs", html, "GRAPHS_smart");
	free(html);

	/* A service with neither default graph nor GRAPHS_ renders no graph section. */
	html = render_log("nograph", 0);
	expect_not_contains("no graph config, no section", html, "begingraph");
	free(html);

	/* The GRAPHS "name:N" split size applies on status pages too: smart-temp
	 * is listed as smart-temp:6 in GRAPHS, so 12 instances page as 2x6 -
	 * while smart-status (not in GRAPHS, stack-local definition) pages on
	 * the default balanced base of 5 -> 3x4. */
	html = render_log_msg("smart", 0, "", "<!-- linecount=12 -->\nstatus body");
	expect_contains("GRAPHS :N split on status page", html, "service=smart-temp&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=6");
	expect_contains("GRAPHS :N split on status page", html, "service=smart-temp&amp;graph_width=576&amp;graph_height=120&amp;first=7&amp;count=6");
	expect_contains("default split without GRAPHS entry", html, "service=smart-status&amp;graph_width=576&amp;graph_height=120&amp;first=1&amp;count=4");
	free(html);

	/* Instance-suffixed entries must reach the URL verbatim: "smart-temp.sda"
	 * prefix-matches the smart-temp gdef but the link must not be rewritten
	 * to the base graph, and "tcp.smtp" must not trip the tcp special-case
	 * into "tcp:tcp.smtp" (its gdef is prefix-matched too - GRAPHS has tcp). */
	html = render_log("dotted", 0);
	expect_contains("dotted entry kept verbatim", html, "service=smart-temp.sda&amp;");
	expect_contains("bundle instance kept verbatim", html, "service=tcp.smtp&amp;");
	expect_not_contains("no doubled tcp special-case", html, "service=tcp:tcp.smtp");
	free(html);

	/* A set-but-empty GRAPHS_<service> must not emit an empty graph section
	 * on a page that previously had none. */
	html = render_log("emptyval", 0);
	expect_not_contains("empty GRAPHS_ value ignored", html, "begingraph");
	free(html);

	/* Reverse tests (flag 'R') collect no RRD data, so GRAPHS_<service>
	 * must not render dead graph links for them. */
	html = render_log_msg("smart", 0, "oRdastle", "status body");
	expect_not_contains("reverse test has no graphs", html, "begingraph");
	free(html);

	/* The value echoed into the page comment is HTML-quoted, so a hostile
	 * value cannot break out of it (GRAPHS_hostile is "a-->b"). */
	html = render_log("hostile", 0);
	expect_contains("hostile value quoted in comment", html, "GRAPHS_hostile: a--&gt;b");
	expect_not_contains("hostile value quoted in comment", html, "GRAPHS_hostile: a-->b");
	free(html);

	/* Rendering a GRAPHS_ page must not corrupt the shared graph table: the
	 * legacy code overwrote the default gdef's name with the custom entry,
	 * breaking every later lookup in the process ("mut" maps to la via
	 * TEST2RRD and has GRAPHS_mut=smart-temp). */
	html = render_log_msg("mut", 0, "", "status body");
	free(html);
	{
		xymongraph_t *la = find_xymon_graph("la");
		if (!la || strcmp(la->xymonrrdname, "la") != 0) {
			fprintf(stderr, "shared gdef mutated: find_xymon_graph(\"la\") %s\n",
				la ? la->xymonrrdname : "returns NULL");
			failures++;
		}
	}

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
