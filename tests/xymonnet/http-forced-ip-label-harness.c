/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/xymonnet/http-forced-ip-label-harness.c
 *
 * urlwithforcedip() is what makes an http/sslcert test's forced target address
 * visible (issue #958): several tests that share one canonical URL but force
 * different addresses must get distinct status labels, and so distinct RRD
 * files. It takes decode_url()'s canonical URL and the parsed desturl, and puts
 * the "=forcedIP" back after the host -- but only when the address was forced
 * (desturl->ipforced), never when it came from DNS, so a plain test's label is
 * left byte-for-byte alone.
 *
 * Each case runs the real decode_url() on a spec, then urlwithforcedip() on its
 * result, and checks the whole contract: the forced address appears, exactly
 * once and only as "=IP" after the host (removing it yields the canonical URL
 * unchanged); the credentials decode_url() stripped never reappear; and a
 * non-forced spec comes back identical to the canonical URL. A mismatch prints
 * the case and exits non-zero.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static int failures = 0;

/* Return a copy of s with the first occurrence of cut removed (cut must be present). */
static char *without(const char *s, const char *cut)
{
	const char *at = strstr(s, cut);
	size_t head = at - s;
	char *r = malloc(strlen(s) + 1);
	memcpy(r, s, head);
	strcpy(r + head, at + strlen(cut));
	return r;
}

static void expect_eq(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
		failures++;
	}
}

static void expect_absent(const char *what, const char *hay, const char *needle)
{
	if (strstr(hay, needle)) {
		printf("FAIL %s: \"%s\" still contains \"%s\"\n", what, hay, needle);
		failures++;
	}
}

/* A forced spec: the label carries "=ip", drops credentials, and is otherwise the canonical URL. */
static void forced(const char *spec, const char *ip, const char *wantlabel)
{
	weburl_t u;
	char *s = strdup(spec);		/* decode_url() writes into its argument */
	char *canon, *label, *stripped;
	char eq[128];

	canon = strdup(decode_url(s, &u));
	label = urlwithforcedip(canon, u.desturl);

	expect_eq(spec, label, wantlabel);

	/* removing the single "=ip" must give back the canonical URL, nothing else changed */
	snprintf(eq, sizeof(eq), "=%s", ip);
	if (strstr(label, eq)) {
		stripped = without(label, eq);
		expect_eq(spec, stripped, canon);
		expect_absent(spec, stripped, eq);	/* exactly one "=ip" */
		free(stripped);
	}
	else {
		printf("FAIL %s: label \"%s\" has no \"%s\"\n", spec, label, eq);
		failures++;
	}

	xfree(label);
	free(canon);
	free_weburl(&u);
	free(s);
}

/* A non-forced spec: the label is the canonical URL, byte for byte. */
static void plain(const char *spec)
{
	weburl_t u;
	char *s = strdup(spec);
	char *canon, *label;

	canon = strdup(decode_url(s, &u));
	label = urlwithforcedip(canon, u.desturl);
	expect_eq(spec, label, canon);

	xfree(label);
	free(canon);
	free_weburl(&u);
	free(s);
}

int main(void)
{
	/* Basic-auth in the spec; decode_url() strips it, the label must not bring it back. */
	forced("http://bob:pw1@example.com=1.2.3.4/page.txt", "1.2.3.4",
	       "http://example.com=1.2.3.4/page.txt");
	expect_absent("creds-ipv4", "http://example.com=1.2.3.4/page.txt", "bob");

	/* No port, a path, and a different address: the dual-stack sibling of the same URL. */
	forced("https://u:p@www.example.org=203.0.113.9/a/b", "203.0.113.9",
	       "https://www.example.org=203.0.113.9/a/b");

	/* A port in the spec: the forced address goes AFTER the ":port", so an IPv6
	   address cannot run into the port and read as a different address (#958). */
	forced("https://example.com:8443=2606:4700::1/x", "2606:4700::1",
	       "https://example.com:8443=2606:4700::1/x");
	forced("http://example.com:8443=1.2.3.4/x", "1.2.3.4",
	       "http://example.com:8443=1.2.3.4/x");

	/* The same canonical URLs without a forced address: the label is unchanged. */
	plain("http://example.com:18090/page.txt");
	plain("https://www.example.org/a/b");

	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
