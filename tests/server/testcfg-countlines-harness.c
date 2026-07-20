/* A test.cfg COUNTLINES metric joins the line-counting (--multigraphs) set:
 * the status page derives its graph-paging linecount from the body lines,
 * even for a column absent from the built-in/env multigraphs list. A column
 * without COUNTLINES (and not in the list) keeps linecount 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static int failures = 0;

static void expect_contains(const char *label, const char *text, const char *needle)
{
	if (!text || !strstr(text, needle)) { fprintf(stderr, "%s: missing '%s'\n", label, needle); failures++; }
}

/* firstline contains '/' so the df-header line-count adjustment does not
 * fire, making the counted value exact. Body has three non-blank lines. */
static char *render(const char *service)
{
	char *html = NULL; size_t sz = 0; FILE *out = open_memstream(&html, &sz);
	if (!out) { perror("open_memstream"); exit(2); }
	generate_html_log("h", "h", (char *)service, "127.0.0.1", COL_GREEN, 0, "s", "",
			  0, "0 min", "green / ok",
			  "line one\nline two\nline three\n",
			  NULL, 0, NULL, NULL, 0, NULL,
			  0, 1, 0, 0, NULL, NULL, NULL, NULL, NULL, 3600, out);
	fclose(out);
	return html;
}

int main(void)
{
	char *html;
	histlocation = HIST_NONE;

	/* "clx": test.cfg gives it COUNTLINES; it has a graph (GRAPHS=clx,
	 * self-mapped). It is NOT in the built-in multigraphs list, so only
	 * COUNTLINES can put it into line-counting -> linecount=3. */
	html = render("clx");
	expect_contains("COUNTLINES metric counts body lines", html, "<!-- linecount=3 -->");
	free(html);

	/* "plain": a graph but no COUNTLINES and not in multigraphs -> linecount 0. */
	html = render("plain");
	expect_contains("non-member column keeps linecount 0", html, "<!-- linecount=0 -->");
	free(html);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
