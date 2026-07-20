/* test.cfg's per-test GRAPHS override wins over the GRAPHS_<service>
 * environment on the status page; a service with no test.cfg GRAPHS falls
 * back to the env. Drives the real generate_html_log(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static int failures = 0;

static void expect_contains(const char *label, const char *text, const char *needle)
{
	if (!text || !strstr(text, needle)) { fprintf(stderr, "%s: missing '%s'\n", label, needle); failures++; }
}
static void expect_not(const char *label, const char *text, const char *needle)
{
	if (text && strstr(text, needle)) { fprintf(stderr, "%s: unexpected '%s'\n", label, needle); failures++; }
}

static char *render(const char *service)
{
	char *html = NULL; size_t sz = 0; FILE *out = open_memstream(&html, &sz);
	if (!out) { perror("open_memstream"); exit(2); }
	generate_html_log("h", "h", (char *)service, "127.0.0.1", COL_GREEN, 0, "s", "",
			  0, "0 min", "green ok", "status body",
			  NULL, 0, NULL, NULL, 0, NULL,
			  0, 1, 0, 0, NULL, NULL, NULL, NULL, NULL, 3600, out);
	fclose(out);
	return html;
}

int main(void)
{
	char *html;
	histlocation = HIST_NONE;

	/* "smart": test.cfg GRAPHS override -> its list, NOT the GRAPHS_smart env. */
	html = render("smart");
	expect_contains("test.cfg GRAPHS override applied", html, "service=tc_temp");
	expect_contains("test.cfg GRAPHS override applied", html, "service=tc_status");
	expect_not("env GRAPHS_smart NOT used when test.cfg overrides", html, "service=env_only");
	free(html);

	/* "legacy": no test.cfg section -> falls back to GRAPHS_legacy env. */
	html = render("legacy");
	expect_contains("env fallback when no test.cfg GRAPHS", html, "service=env_graph");
	free(html);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
