/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/web/cgi-pageregex-harness.c
 *
 * Drives cgi_pageregex() (lib/cgi.c), used by cgi-pageregex.sh.
 *
 * The CGIs ask xymond for the page the viewer came from and its sub-pages,
 * "page=^PAGE$|^PAGE/.+". cgi_pageregex() builds that with PAGE escaped, so
 * it matches that page and no other. Checked two ways: the text it returns,
 * and what that text matches once compiled the way xymond compiles it
 * (compileregex()). A value with a space or a control character gives NULL.
 *
 * Prints a line per failure and exits 1 if there was one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libxymon.h"

static int failures = 0;

static void text(const char *input, const char *expected)
{
	char *got = cgi_pageregex(input);

	if ((!got != !expected) || (got && strcmp(got, expected))) {
		printf("FAIL cgi_pageregex(\"%s\") = \"%s\", expected \"%s\"\n",
		       input ? input : "(null)", got ? got : "NULL", expected ? expected : "NULL");
		failures++;
	}
	if (got) xfree(got);	/* NULL is an expected answer, and xfree() refuses NULL */
}

/* Does cgi_pageregex(page) match the page path candidate? */
static void match(const char *page, const char *candidate, int expected)
{
	char *re = cgi_pageregex(page);
	pcre2_code *code;
	int got;

	if (!re) { printf("FAIL cgi_pageregex(\"%s\") = NULL\n", page); failures++; return; }
	code = compileregex(re);
	if (!code) { printf("FAIL \"%s\" does not compile\n", re); failures++; xfree(re); return; }
	got = (matchregex(candidate, code) != 0);
	if (got != expected) {
		printf("FAIL \"%s\" %s \"%s\", expected the opposite\n", re, got ? "matches" : "does not match", candidate);
		failures++;
	}
	freeregex(code);
	xfree(re);
}

int main(void)
{
	/* The text: the page and its sub-pages, only letters and digits bare */
	text("ops", "^ops$|^ops/.+");
	text("ops.eu", "^ops\\.eu$|^ops\\.eu/.+");
	text("servers/linux", "^servers\\/linux$|^servers\\/linux/.+");

	/* Refused, never passed on: a space would start another filter */
	text(NULL, NULL);
	text("", NULL);
	text("ops color=red", NULL);
	text("ops\tx", NULL);
	text("ops\n", NULL);
	text("ops\x01x", NULL);
	text("ops\x7f", NULL);

	/* What it matches: the page and its sub-pages, nothing it only resembles */
	match("ops", "ops", 1);
	match("ops", "ops/web", 1);
	match("ops", "devops", 0);
	match("ops", "ops2", 0);
	match("ops", "servers/ops", 0);
	match("ops.eu", "ops.eu", 1);
	match("ops.eu", "opsXeu", 0);
	match("servers/linux", "servers/linux/db", 1);
	match("servers/linux", "servers/linuxes", 0);

	return failures ? 1 : 0;
}
