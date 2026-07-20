/* Reversible, collision-free RRD instance encoding: rrdinstance_encode()
 * maps any instance string to a filename-safe form that rrdinstance_decode()
 * turns back into the original byte-for-byte, and distinct instances never
 * collapse to the same file (the flaw in the legacy '/'->',' scheme). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rrdinstance.h"

static int failures = 0;

static void expect_eq(const char *label, const char *got, const char *want)
{
	if (!got || strcmp(got, want) != 0) {
		fprintf(stderr, "%s: got '%s', wanted '%s'\n", label, got ? got : "(null)", want);
		failures++;
	}
}

/* decode(encode(s)) must reproduce s exactly, for any bytes. */
static void expect_roundtrip(const char *label, const char *s)
{
	char *enc = rrdinstance_encode(s);
	char *dec = rrdinstance_decode(enc);

	if (!enc || !dec || strcmp(dec, s) != 0) {
		fprintf(stderr, "%s: '%s' -> '%s' -> '%s' (round-trip broken)\n",
			label, s, enc ? enc : "(null)", dec ? dec : "(null)");
		failures++;
	}
	free(enc);
	free(dec);
}

int main(void)
{
	char *a, *b;
	char every[257];
	int i;

	/* Safe bytes pass through untouched: instances like device or interface
	 * names must keep producing exactly the filenames they always have. */
	a = rrdinstance_encode("");        expect_eq("empty", a, "");            free(a);
	a = rrdinstance_encode("ada0");    expect_eq("device", a, "ada0");       free(a);
	a = rrdinstance_encode("eth0.0");  expect_eq("iface", a, "eth0.0");      free(a);
	a = rrdinstance_encode("sd-a_1");  expect_eq("punct", a, "sd-a_1");      free(a);
	a = rrdinstance_encode("a,b");     expect_eq("comma", a, "a,b");         free(a);

	/* The path separator - the whole reason for an encoding - becomes %2F. */
	a = rrdinstance_encode("/");       expect_eq("slash", a, "%2F");         free(a);
	a = rrdinstance_encode("/var");    expect_eq("mount", a, "%2Fvar");      free(a);
	a = rrdinstance_encode("a b");     expect_eq("space", a, "a%20b");       free(a);
	a = rrdinstance_encode("50%");     expect_eq("percent", a, "50%25");     free(a);

	/* Collision freedom: the two names the legacy scheme aliased must map to
	 * distinct files now. */
	a = rrdinstance_encode("/a/b");
	b = rrdinstance_encode("/a,b");
	if (a && b && strcmp(a, b) == 0) {
		fprintf(stderr, "collision: '/a/b' and '/a,b' both encode to '%s'\n", a);
		failures++;
	}
	free(a); free(b);

	/* decode() leaves non-encoded and malformed input verbatim, so names
	 * written by other back-ends (or by hand) are never corrupted. */
	a = rrdinstance_decode("eth0.0"); expect_eq("passthru", a, "eth0.0");    free(a);
	a = rrdinstance_decode("%2");     expect_eq("truncated", a, "%2");       free(a);
	a = rrdinstance_decode("%zz");    expect_eq("nonhex", a, "%zz");         free(a);

	/* decode_ifencoded() only fires on canonical encoder output: legacy
	 * names that merely contain %XX byte runs (URLs in tcp.http captures)
	 * carry raw bytes outside the safe set and must stay verbatim. */
	a = rrdinstance_decode_ifencoded("%2Fvar");
	expect_eq("ifenc-mount", a, "/var"); free(a);
	a = rrdinstance_decode_ifencoded("eth0");
	if (a) { fprintf(stderr, "ifenc-plain: no-escape name decoded to '%s'\n", a); failures++; free(a); }
	a = rrdinstance_decode_ifencoded("https:,,host,a%20b");
	if (a) { fprintf(stderr, "ifenc-url: legacy URL name decoded to '%s'\n", a); failures++; free(a); }
	a = rrdinstance_decode_ifencoded("a%2fb");
	if (a) { fprintf(stderr, "ifenc-lowercase: non-canonical escape decoded to '%s'\n", a); failures++; free(a); }
	a = rrdinstance_decode_ifencoded("50%");
	if (a) { fprintf(stderr, "ifenc-stray: stray %% decoded to '%s'\n", a); failures++; free(a); }

	/* Round-trip a range of awkward instances... */
	expect_roundtrip("rt-empty", "");
	expect_roundtrip("rt-mount", "/var/log/xymon");
	expect_roundtrip("rt-comma", "/a,b");
	expect_roundtrip("rt-mixed", "C:\\Program Files\\App");
	expect_roundtrip("rt-percent", "100%done");
	expect_roundtrip("rt-space", "some mount point");

	/* ...and every single non-NUL byte value. */
	for (i = 1; i <= 256; i++) every[i-1] = (char)(i & 0xff);
	every[256] = '\0';
	expect_roundtrip("rt-allbytes", every);

	if (failures) {
		fprintf(stderr, "%d assertion(s) failed\n", failures);
		return 1;
	}
	return 0;
}
