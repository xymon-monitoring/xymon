/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/web/cgi-split-hostsvc-harness.c
 *
 * Drives the REAL CGI-value confinement helpers from lib/cgi.c (#147).
 *
 * Two confinements, by category:
 *   - cgi_pathcomponent() -- STRICT, for values that are looked-up names or
 *     become filenames: HOST/CLIENT/TIMEBUF and BOTH halves of the
 *     HOSTSVC/HISTFILE split (the split service is a column name that
 *     history.cgi shells out to `tail`, so it must be shell-safe). A single
 *     component of the strict name charset (ASCII letters/digits + ":,._-")
 *     or refused as a WHOLE (NULL), never truncated or normalized. A stray
 *     byte (space, '/', '(', '\', ';', a control char, a UTF-8 byte)
 *     rejects the whole value; ".", ".." and "" are refused; and the
 *     comma-decoded spelling ("," -> ".") cannot slip a traversal through.
 *     comma-decoding is per-parameter (HOST verbatim, CLIENT/HISTFILE/HOSTSVC
 *     decode the 192,168,1,1 spelling). Non-ASCII names are refused for now
 *     -- widening this charset is a separate policy change.
 *   - cgi_component() -- FREE-FORM, only for svcstatus's direct SERVICE (a
 *     column name or a PCRE testname pattern for the aggregate view) and
 *     section leaves: metacharacters allowed, refused only on a control
 *     byte (protocol injection) or traversal. It reaches a regex/query or a
 *     re-confined histlog path, never a shell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *cgi_pathcomponent(const char *value, int decode_commas);
extern char *cgi_component(const char *value);
extern void cgi_split_hostsvc(char *value, char **hostname, char **service);

static int failures = 0;

static const char *show(const char *s)
{
	return s ? s : "NULL";
}

static int same(const char *got, const char *expected)
{
	if (!expected || !got) return (expected == got);
	return (strcmp(got, expected) == 0);
}

static void checkpc(const char *input, int decode_commas, const char *expected)
{
	char *got = cgi_pathcomponent(input, decode_commas);

	if (!same(got, expected)) {
		printf("FAIL pathcomponent('%s', %d) -> '%s' (expected '%s')\n",
		       show(input), decode_commas, show(got), show(expected));
		failures++;
	}
	free(got);
}

static void checkcomp(const char *input, const char *expected)
{
	char *got = cgi_component(input);

	if (!same(got, expected)) {
		printf("FAIL component('%s') -> '%s' (expected '%s')\n",
		       show(input), show(got), show(expected));
		failures++;
	}
	free(got);
}

static void check(const char *input, const char *exphost, const char *expsvc)
{
	char *buf = input ? strdup(input) : NULL;
	char *host = NULL, *svc = NULL;

	cgi_split_hostsvc(buf, &host, &svc);
	if (!same(host, exphost) || !same(svc, expsvc)) {
		printf("FAIL split('%s') -> host='%s' svc='%s' (expected '%s' / '%s')\n",
		       show(input), show(host), show(svc), show(exphost), show(expsvc));
		failures++;
	}
	free(buf);
	free(host);
	free(svc);
}

int main(void)
{
	/* ---- cgi_pathcomponent(): traversal-shaped values are refused whole
	 * (NULL), never normalized into something servable ("../realhost"
	 * must not become "realhost"). */
	checkpc(NULL, 0, NULL);
	checkpc("", 0, NULL);
	checkpc(".", 0, NULL);
	checkpc("..", 0, NULL);
	checkpc("a/b", 0, NULL);
	checkpc("../realhost", 0, NULL);
	checkpc("realhost/", 0, NULL);
	checkpc("cpu/..", 0, NULL);

	/* Legal single components pass through unchanged: FQDN dots, IPv6
	 * colons, hyphens/underscores. */
	checkpc("web.grp", 0, "web.grp");
	checkpc("fe80::1", 0, "fe80::1");
	checkpc("web-front_01", 0, "web-front_01");

	/* A byte outside the legal set refuses the WHOLE value -- not
	 * truncated to a servable prefix. This is one rule for every
	 * parameter: a space, a control byte (a newline would otherwise
	 * inject a second xymond protocol line), a '(' or a UTF-8 byte all
	 * reject. '\' is excluded too -- safe_basename() only special-cases
	 * '/', so a backslash must not survive onto a Windows/SMB backend
	 * where it separates directories. */
	checkpc("web bar", 0, NULL);
	checkpc("lab(test)", 0, NULL);
	checkpc("DOM\\server", 0, NULL);
	checkpc("..\\..\\etc", 0, NULL);
	checkpc("cpu\nconfig", 0, NULL);
	checkpc("cpu\rconfig", 0, NULL);
	checkpc("cpu\tx", 0, NULL);
	checkpc("cpu\177x", 0, NULL);
	checkpc("\033[0m", 0, NULL);
	checkpc("h\303\264te", 0, NULL);	/* UTF-8: deferred to the charset policy issue */

	/* comma-decoding is per-parameter: HOST arrives verbatim (a literal
	 * ',' stays reachable), CLIENT/HOSTSVC decode the 192,168,1,1
	 * spelling -- and "," / ",," must not slip through as "." / ".." once
	 * decoded. */
	checkpc("a,b", 0, "a,b");
	checkpc("a,b", 1, "a.b");
	checkpc("192,168,1,1", 1, "192.168.1.1");
	checkpc(",", 1, NULL);
	checkpc(",,", 1, NULL);

	/* ---- cgi_component(): the free-form service policy. A column name,
	 * a client section leaf, or a PCRE testname pattern -- metacharacters
	 * are ALLOWED (svcstatus compiles SERVICE as a regex for the aggregate
	 * view), so only control bytes and traversal reject. */
	checkcomp("cpu", "cpu");
	checkcomp("cpu|disk", "cpu|disk");	/* aggregate/regex SERVICE */
	checkcomp("^(a|b)$", "^(a|b)$");
	checkcomp("disk.*", "disk.*");
	checkcomp("cpu[0-9]", "cpu[0-9]");
	checkcomp("Disk Usage", "Disk Usage");	/* free-form section leaf */
	checkcomp("h\303\264te", "h\303\264te");	/* non-ASCII ok: not a looked-up name */
	checkcomp("cpu\ndisk", NULL);	/* control byte -> would inject a protocol line */
	checkcomp("cpu\r", NULL);
	checkcomp("a/b", NULL);		/* traversal */
	checkcomp(".", NULL);
	checkcomp("..", NULL);
	checkcomp("", NULL);
	checkcomp(NULL, NULL);

	/* ---- cgi_split_hostsvc(): legitimate values, including the
	 * comma-spelled IP form. A '\'-bearing host half is refused (NULL),
	 * so the callers reject the request. */
	check("realhost.cpu", "realhost", "cpu");
	check("192,168,1,1.conn", "192.168.1.1", "conn");
	check("DOM\\server.cpu", NULL, "cpu");

	/* Both halves take the STRICT confinement: the split service becomes a
	 * filename that history.cgi shells out to tail, so a space (or any
	 * out-of-charset byte) in the service half refuses it whole -- it is not
	 * trimmed to "cpu" and served, and cannot carry a shell metacharacter. */
	check("realhost.cpu ", "realhost", NULL);
	check("realhost.conn;id", "realhost", NULL);
	check("www,example,com.conn\r", "www.example.com", NULL);

	/* A '/'-bearing value is refused whole, never normalized into a
	 * servable prefix. */
	check("realhost.cpu/..", NULL, NULL);
	check("realhost.cpu/../../etc/passwd", NULL, NULL);

	/* A backslash anywhere refuses the host half (it is a separator on
	 * Windows/SMB backends), so both components come back refused. */
	check("realhost.cpu\\..", NULL, NULL);

	/* Traversal-shaped components come back NULL, and "," / ",," must not
	 * slip through as "." / ".." via the comma decode. */
	check("..", NULL, NULL);
	check("...", NULL, NULL);
	check(",,.conn", NULL, "conn");
	check("realhost.", "realhost", NULL);
	check("", NULL, NULL);

	/* A NULL value (cgi_request() yields value==NULL on a bodyless
	 * multipart part) must be refused, not dereferenced. */
	check(NULL, NULL, NULL);

	return failures ? 1 : 0;
}
