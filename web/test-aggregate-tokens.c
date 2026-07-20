/*
 * test-aggregate-tokens.c
 *
 * Golden-output tests for the @AVG:/@SUM:/@MIN:/@MAX:/@COUNT:/@MEDIAN:/@STDEV:/
 * @PERCENT:/@Pxx: aggregate token expansion logic added to web/showgraph.c.
 *
 * Build & run (from web/):
 *   make test-aggregate-tokens && ./test-aggregate-tokens
 *
 * The aggregate helpers in showgraph.c are static and depend on
 * file-scope globals (rrddbcount, firstidx, lastidx) and on libxymon's
 * strbuffer API. This test #include's the same source the production
 * code includes (web/aggregate-tokens.inc.c), so parser and tests
 * cannot drift. A minimal in-test strbuffer shim avoids pulling in
 * libxymon, keeping the test runnable in isolation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- minimal strbuffer shim (in-test only) ---- */
typedef struct { char *s; int len; int cap; } strbuffer_t;
static strbuffer_t *newstrbuffer(int cap) {
	strbuffer_t *b = calloc(1, sizeof(*b));
	b->cap = cap < 64 ? 64 : cap;
	b->s = calloc(1, b->cap);
	return b;
}
static void buf_grow(strbuffer_t *b, int extra) {
	while (b->len + extra + 1 > b->cap) { b->cap *= 2; b->s = realloc(b->s, b->cap); }
}
static void addtobuffer(strbuffer_t *b, char *t) {
	int n = strlen(t); buf_grow(b, n); memcpy(b->s + b->len, t, n); b->len += n; b->s[b->len] = 0;
}
static void addtobufferraw(strbuffer_t *b, char *t, int n) {
	buf_grow(b, n); memcpy(b->s + b->len, t, n); b->len += n; b->s[b->len] = 0;
}
#define STRBUF(x) ((x)->s)

/* ---- file-scope context the helpers read ---- */
static int rrddbcount = 0;
static int firstidx = -1;
static int lastidx = 0;

/* Minimal rrddb shim: selected_rrdidx() consults flatvals (virtual flat
 * instances are skipped by per-file emitters). The unit tests use real
 * (non-flat) slots only, so a NULL-filled table suffices. */
typedef struct { char *flatvals; } rrddb_shim_t;
static rrddb_shim_t rrddbs[64];

/* Helpers under test come from the same source web/showgraph.c uses.
 * Including the .inc.c (rather than maintaining a copy) keeps the test
 * locked to the production parser; any future fix in
 * aggregate-tokens.inc.c is automatically reflected here. */
#include "aggregate-tokens.inc.c"

/* Drive expansion of a single token; returns malloc'd RPN string. */
static char *expand_one(char *tpl) {
	char *op, *name; int oplen, toklen;
	strbuffer_t *r = newstrbuffer(128);
	if (!is_aggregate_token(tpl, &op, &name, &oplen, &toklen)) {
		free(r->s); free(r); return strdup("(not-aggregate)");
	}
	add_aggregate_rpn(r, op, name, toklen - oplen - 1);
	char *out = strdup(STRBUF(r));
	free(r->s); free(r);
	return out;
}

/* ---- test driver ---- */
static int failures = 0;
static void check(const char *label, char *tpl, int n, int first, int last, const char *want) {
	rrddbcount = n; firstidx = first; lastidx = last;
	aggregate_dscount = 0;
	char *got = expand_one(tpl);
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "FAIL %s: tpl=%s n=%d -> %s (want %s)\n", label, tpl, n, got, want);
		failures++;
	} else {
		printf("ok   %s: %s -> %s\n", label, tpl, got);
	}
	free(got);
}

/* Within-file aggregate driver: sets aggregate_dscount (no rrddbcount /
 * firstidx / lastidx context, since within-file ops don't touch those). */
static void check_ds(const char *label, char *tpl, int dscount, const char *want) {
	rrddbcount = 0; firstidx = -1; lastidx = 0;
	aggregate_dscount = dscount;
	char *got = expand_one(tpl);
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "FAIL %s: tpl=%s dscount=%d -> %s (want %s)\n", label, tpl, dscount, got, want);
		failures++;
	} else {
		printf("ok   %s: %s -> %s\n", label, tpl, got);
	}
	free(got);
	aggregate_dscount = 0;
}

int main(void) {
	/* n = 0 -> UNKN, COUNT -> 0 */
	check("avg-n0", "@AVG:t@", 0, -1, 0, "UNKN");
	check("sum-n0", "@SUM:t@", 0, -1, 0, "UNKN");
	check("count-n0", "@COUNT:t@", 0, -1, 0, "0");
	check("p95-n0", "@P95:t@", 0, -1, 0, "UNKN");

	/* n = 1 */
	check("avg-n1", "@AVG:t@", 1, -1, 0, "t0,1,/");
	check("sum-n1", "@SUM:t@", 1, -1, 0, "t0");
	check("min-n1", "@MIN:t@", 1, -1, 0, "t0");
	check("median-n1", "@MEDIAN:t@", 1, -1, 0, "t0,1,MEDIAN");

	/* n = 3 */
	check("sum-n3", "@SUM:t@", 3, -1, 0, "t0,t1,+,t2,+");
	check("avg-n3", "@AVG:t@", 3, -1, 0, "t0,t1,+,t2,+,3,/");
	check("min-n3", "@MIN:t@", 3, -1, 0, "t0,t1,MIN,t2,MIN");
	check("max-n3", "@MAX:t@", 3, -1, 0, "t0,t1,MAX,t2,MAX");
	check("sumnan-n3", "@SUMNAN:t@", 3, -1, 0, "t0,t1,ADDNAN,t2,ADDNAN");
	check("minnan-n3", "@MINNAN:t@", 3, -1, 0, "t0,t1,MINNAN,t2,MINNAN");
	check("avgnan-n3", "@AVGNAN:t@", 3, -1, 0, "t0,t1,t2,3,AVG");
	check("median-n3", "@MEDIAN:t@", 3, -1, 0, "t0,t1,t2,3,MEDIAN");
	check("stdev-n3", "@STDEV:t@", 3, -1, 0, "t0,t1,t2,3,STDEV");
	check("count-n3", "@COUNT:t@", 3, -1, 0, "t0,UN,0,1,IF,t1,UN,0,1,IF,+,t2,UN,0,1,IF,+");
	check("p95-n3", "@P95:t@", 3, -1, 0, "t0,t1,t2,95,3,PERCENT");
	check("percent-n3", "@PERCENT:t:90@", 3, -1, 0, "t0,t1,t2,90,3,PERCENT");
	check("p99.9-n3", "@P99.9:t@", 3, -1, 0, "t0,t1,t2,99.9,3,PERCENT");

	/* firstidx/lastidx slicing: only idx 1..2 selected of 4 */
	check("avg-slice", "@AVG:t@", 4, 1, 2, "t1,t2,+,2,/");
	check("count-slice", "@COUNT:t@", 4, 1, 2, "t1,UN,0,1,IF,t2,UN,0,1,IF,+");

	/* Slicing edge cases */
	check("avg-slice-equal", "@AVG:t@", 4, 2, 2, "t2,1,/");                  /* single element */
	check("count-slice-oor", "@COUNT:t@", 4, 10, 20, "0");                   /* fully out of range */
	check("avg-slice-rev",  "@AVG:t@", 4, 3, 1, "UNKN");                     /* lastidx < firstidx -> no selection */

	/* n = 5 and n = 1 STDEV (output well-defined even though stddev of 1 sample is meaningless) */
	check("stdev-n0", "@STDEV:t@", 0, -1, 0, "UNKN");
	check("stdev-n1", "@STDEV:t@", 1, -1, 0, "t0,1,STDEV");
	check("sum-n5",   "@SUM:t@",   5, -1, 4, "t0,t1,+,t2,+,t3,+,t4,+");

	/* @PERCENT:name:pct@ pct validation -- prior version emitted garbage RPN */
	check("percent-bad-pct",     "@PERCENT:t:abc@", 3, -1, 2, "UNKN");
	check("percent-extra-colon", "@PERCENT:t:95:x@", 3, -1, 2, "UNKN");
	check("percent-empty-pct",   "@PERCENT:t:@", 3, -1, 2, "UNKN");
	check("percent-float-pct",   "@PERCENT:t:95.5@", 3, -1, 2, "t0,t1,t2,95.5,3,PERCENT");

	/* Malformed/rejected aggregate tokens -- parser should return false
	 * so the caller treats them as literal text. expand_one signals this
	 * via the "(not-aggregate)" sentinel. */
	check("avg-empty-operand", "@AVG:@",        3, -1, 2, "(not-aggregate)");
	check("avg-no-terminator", "@AVG:tnoterm",  3, -1, 2, "(not-aggregate)");
	check("p-dot-dot",         "@P95.5.5:t@",   3, -1, 2, "(not-aggregate)");
	check("p-leading-dot",     "@P.5:t@",       3, -1, 2, "(not-aggregate)");
	check("p-no-digit",        "@P:t@",         3, -1, 2, "(not-aggregate)");
	check("p-trailing-dot",    "@P9.:t@",       3, -1, 2, "t0,t1,t2,9.,3,PERCENT"); /* trailing dot accepted today; flag if rejecting later */

	/* Name delimiter check: whitespace / commas inside the operand
	 * would otherwise let the parser swallow downstream @TOKEN@ markers. */
	check("avg-space-in-name", "@AVG:foo bar@",   3, -1, 2, "(not-aggregate)");
	check("avg-tab-in-name",   "@AVG:foo\tbar@", 3, -1, 2, "(not-aggregate)");
	check("avg-comma-in-name", "@AVG:foo,bar@",   3, -1, 2, "(not-aggregate)");
	check("avg-dot-in-name",   "@AVG:foo.bar@",   3, -1, 2, "foo.bar0,foo.bar1,+,foo.bar2,+,3,/"); /* dots allowed (legal in some uses) */

	/* Within-file aggregate: @DSMEDIAN:prefix@ walks prefix1..prefixN. */
	check_ds("dsmedian-n3",     "@DSMEDIAN:ping@", 3, "ping1,ping2,ping3,3,MEDIAN");
	check_ds("dsmedian-n1",     "@DSMEDIAN:ping@", 1, "ping1,1,MEDIAN");
	check_ds("dsmedian-n20",    "@DSMEDIAN:p@",    20,
	         "p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15,p16,p17,p18,p19,p20,20,MEDIAN");
	check_ds("dsmedian-n0",     "@DSMEDIAN:ping@", 0, "UNKN");
	check_ds("dsmedian-empty",  "@DSMEDIAN:@",     5, "(not-aggregate)");
	check_ds("dsmedian-noterm", "@DSMEDIAN:ping",  5, "(not-aggregate)");

	if (failures) { fprintf(stderr, "\n%d failure(s)\n", failures); return 1; }
	printf("\nAll tests passed.\n");
	return 0;
}
