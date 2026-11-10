/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/web/cgi-hostregex-harness.c
 *
 * Drives cgi_hostregex() (lib/cgi.c), used by cgi-hostregex.sh.
 *
 * The CGIs ask xymond for "host=^NAME$". cgi_hostregex() turns NAME into a
 * pattern that matches that name and no other: each character that is not a
 * letter or a digit gets a backslash. Checked two ways: the text it returns,
 * and what that text matches once compiled the way xymond compiles it
 * (compileregex()). A value that is not a host name gives NULL.
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
	char *got = cgi_hostregex(input);

	if ((!got != !expected) || (got && strcmp(got, expected))) {
		printf("FAIL cgi_hostregex(\"%s\") = \"%s\", expected \"%s\"\n",
		       input ? input : "(null)", got ? got : "NULL", expected ? expected : "NULL");
		failures++;
	}
	if (got) xfree(got);	/* NULL is an expected answer, and xfree() refuses NULL */
}

/* Does "^<cgi_hostregex(name)>$" match candidate? */
static void match(const char *name, const char *candidate, int expected)
{
	char *re = cgi_hostregex(name), *anchored;
	pcre2_code *code;
	int got;

	if (!re) { printf("FAIL cgi_hostregex(\"%s\") = NULL\n", name); failures++; return; }
	anchored = (char *)malloc(strlen(re) + 3);
	sprintf(anchored, "^%s$", re);
	code = compileregex(anchored);
	if (!code) { printf("FAIL \"%s\" does not compile\n", anchored); failures++; xfree(re); xfree(anchored); return; }
	got = (matchregex(candidate, code) != 0);
	if (got != expected) {
		printf("FAIL \"%s\" %s \"%s\", expected the opposite\n",
		       anchored, got ? "matches" : "does not match", candidate);
		failures++;
	}
	freeregex(code);
	xfree(re);
	xfree(anchored);
}

int main(void)
{
	/* The text: only letters and digits stay bare */
	text("web01", "web01");
	text("web01.example.com", "web01\\.example\\.com");
	text("a-b_c", "a\\-b\\_c");
	text("fe80::1", "fe80\\:\\:1");
	text("web01,example,com", "web01\\,example\\,com");

	/* Not a host name: refused, never passed on (a space would start
	   another filter in the query) */
	text(NULL, NULL);
	text("", NULL);
	text("web01 color=red", NULL);
	text("web01.*", NULL);
	text("a/b", NULL);
	text("web01$", NULL);

	/* What it matches: the name, and not a name it only resembles */
	match("web01.example.com", "web01.example.com", 1);
	match("web01.example.com", "web01-example.com", 0);
	match("web01.example.com", "web01Xexample.com", 0);
	match("web01.example.com", "web01.example.com.au", 0);
	match("web01.example.com", "xweb01.example.com", 0);
	match("fe80::1", "fe80::1", 1);
	match("a-b_c", "a-b_c", 1);

	return failures ? 1 : 0;
}
