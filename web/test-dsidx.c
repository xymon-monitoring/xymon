/*
 * test-dsidx.c
 *
 * Golden-output tests for expand_dsidx_in_block() and its helpers,
 * shared with web/showgraph.c via dsidx.inc.c - the tests always
 * exercise the production code.
 *
 * Build & run (from web/):
 *   make test-dsidx && ./test-dsidx
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ---- minimal shim mirroring gdef_t (only fields the helpers touch) ---- */
typedef struct { int dscount; int dsidx_runtime; char **defs; char **rawdefs; } gdef_t;

/* ---- the production helpers, included so tests always exercise them ---- */
#include "dsidx.inc.c"

/* ---- test driver ---- */
static int failures = 0;
static void check_line(const char *label, char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "FAIL %s: got %s (want %s)\n", label, got, want);
		failures++;
	} else {
		printf("ok   %s: %s\n", label, got);
	}
}

static gdef_t *gd_from(int dscount, ...)
{
	gdef_t *g = calloc(1, sizeof(*g));
	g->dscount = dscount;
	/* Read NULL-terminated argv-style list */
	const char *args[64]; int n = 0;
	va_list ap; va_start(ap, dscount);
	const char *s;
	while ((s = va_arg(ap, const char *)) != NULL) args[n++] = s;
	va_end(ap);
	g->defs = calloc(n + 1, sizeof(char *));
	int i;
	for (i = 0; i < n; i++) g->defs[i] = strdup(args[i]);
	g->defs[n] = NULL;
	return g;
}

static void free_gd(gdef_t *g)
{
	int i;
	for (i = 0; g->defs[i]; i++) free(g->defs[i]);
	free(g->defs);
	free(g);
}

int main(void)
{
	unsetenv("SMOKEPINGSAMPLES");

	/* dscount=0 + no @DSIDX@ -> no-op, no runtime flag */
	{
		gdef_t *g = gd_from(0, "TITLE plain", (char *)NULL);
		expand_dsidx_in_block(g);
		check_line("noop-no-template", g->defs[0], "TITLE plain");
		if (g->dsidx_runtime) {
			fprintf(stderr, "FAIL noop-no-template: dsidx_runtime should be 0\n");
			failures++;
		} else printf("ok   noop-no-template-flag\n");
		free_gd(g);
	}

	/* dscount=0 + @DSIDX@ present -> defer to render time:
	 * templates left intact, dsidx_runtime set. (No more parse-time
	 * env fallback: render time derives N per-file.) */
	{
		gdef_t *g = gd_from(0, "DEF:p@DSIDX@=x:p@DSIDX@", (char *)NULL);
		expand_dsidx_in_block(g);
		check_line("defer-keeps-template", g->defs[0], "DEF:p@DSIDX@=x:p@DSIDX@");
		if (!g->dsidx_runtime) {
			fprintf(stderr, "FAIL defer-keeps-template: dsidx_runtime should be 1\n");
			failures++;
		} else printf("ok   defer-runtime-flag\n");
		free_gd(g);
	}

	/* Explicit DSCOUNT still expands at parse time. */
	{
		gdef_t *g = gd_from(3, "DEF:p@DSIDX@=x", (char *)NULL);
		expand_dsidx_in_block(g);
		check_line("dscount-explicit-1", g->defs[0], "DEF:p1=x");
		check_line("dscount-explicit-3", g->defs[2], "DEF:p3=x");
		if (g->defs[3] != NULL) {
			fprintf(stderr, "FAIL dscount-explicit-stop: expected NULL at idx 3\n");
			failures++;
		} else printf("ok   dscount-explicit-stop\n");
		free_gd(g);
	}

	/* basic loop 1..3 */
	{
		gdef_t *g = gd_from(3, "DEF:p@DSIDX@=x:p@DSIDX@", (char *)NULL);
		expand_dsidx_in_block(g);
		check_line("basic1", g->defs[0], "DEF:p1=x:p1");
		check_line("basic2", g->defs[1], "DEF:p2=x:p2");
		check_line("basic3", g->defs[2], "DEF:p3=x:p3");
		free_gd(g);
	}

	/* @PREVDSIDX@ -> auto-start at 2 */
	{
		gdef_t *g = gd_from(3, "CDEF:s@DSIDX@=p@DSIDX@,p@PREVDSIDX@,-", (char *)NULL);
		expand_dsidx_in_block(g);
		check_line("prev2", g->defs[0], "CDEF:s2=p2,p1,-");
		check_line("prev3", g->defs[1], "CDEF:s3=p3,p2,-");
		free_gd(g);
	}

	/* @DSSTART:N@ prefix is stripped, sets start */
	{
		gdef_t *g = gd_from(3, "@DSSTART:2@STACK:s@DSIDX@#color", (char *)NULL);
		expand_dsidx_in_block(g);
		check_line("dsstart-2", g->defs[0], "STACK:s2#color");
		check_line("dsstart-3", g->defs[1], "STACK:s3#color");
		free_gd(g);
	}

	/* line without tokens emits once */
	{
		gdef_t *g = gd_from(3, "TITLE plain", "DEF:p@DSIDX@=x", (char *)NULL);
		expand_dsidx_in_block(g);
		check_line("plain", g->defs[0], "TITLE plain");
		check_line("after-plain-1", g->defs[1], "DEF:p1=x");
		check_line("after-plain-3", g->defs[3], "DEF:p3=x");
		free_gd(g);
	}

	/* full conn-smoke fragment: ensure slice1 stays single and STACK starts at 2 */
	{
		gdef_t *g = gd_from(4,
			"CDEF:slice1=ping1",
			"CDEF:slice@DSIDX@=ping@DSIDX@,ping@PREVDSIDX@,-",
			"AREA:slice1#FFFFFF",
			"@DSSTART:2@STACK:slice@DSIDX@#color",
			(char *)NULL);
		expand_dsidx_in_block(g);
		check_line("smoke-slice1-literal", g->defs[0], "CDEF:slice1=ping1");
		check_line("smoke-slice2-cdef",   g->defs[1], "CDEF:slice2=ping2,ping1,-");
		check_line("smoke-slice4-cdef",   g->defs[3], "CDEF:slice4=ping4,ping3,-");
		check_line("smoke-area",          g->defs[4], "AREA:slice1#FFFFFF");
		check_line("smoke-stack2",        g->defs[5], "STACK:slice2#color");
		check_line("smoke-stack4",        g->defs[7], "STACK:slice4#color");
		free_gd(g);
	}

	/* Second use-case modelled on SmokePing's DNS probe: same template
	 * shape as conn-smoke but for a different RRD/DS family
	 * (dns.<resolver>-smoke.rrd with DSes q1..qN sorted by response
	 * time). Verifies @DSIDX@ infrastructure isn't conn-specific. */
	{
		gdef_t *g = gd_from(5,
			"DEF:q@DSIDX@=dns.r1-smoke.rrd:q@DSIDX@:AVERAGE",
			"CDEF:s@DSIDX@=q@DSIDX@,q@PREVDSIDX@,-",
			"AREA:q1#00000000",
			"@DSSTART:2@AREA:s@DSIDX@#20A050A0:STACK",
			(char *)NULL);
		expand_dsidx_in_block(g);
		/* DEF expands 1..5 -> 5 lines */
		check_line("dns-def-1",     g->defs[0], "DEF:q1=dns.r1-smoke.rrd:q1:AVERAGE");
		check_line("dns-def-5",     g->defs[4], "DEF:q5=dns.r1-smoke.rrd:q5:AVERAGE");
		/* slice CDEF uses @PREVDSIDX@ -> auto-starts at 2, 4 lines */
		check_line("dns-slice-2",   g->defs[5], "CDEF:s2=q2,q1,-");
		check_line("dns-slice-5",   g->defs[8], "CDEF:s5=q5,q4,-");
		/* invisible base */
		check_line("dns-base",      g->defs[9], "AREA:q1#00000000");
		/* STACK uses @DSSTART:2@ -> 4 lines, starts at idx 2 */
		check_line("dns-stack-2",   g->defs[10], "AREA:s2#20A050A0:STACK");
		check_line("dns-stack-5",   g->defs[13], "AREA:s5#20A050A0:STACK");
		free_gd(g);
	}

	/* dscount=1 + @PREVDSIDX@ -> line drops out cleanly */
	{
		gdef_t *g = gd_from(1,
			"DEF:p@DSIDX@=x:p@DSIDX@",
			"CDEF:s@DSIDX@=p@DSIDX@,p@PREVDSIDX@,-",
			(char *)NULL);
		expand_dsidx_in_block(g);
		check_line("n1-def", g->defs[0], "DEF:p1=x:p1");
		if (g->defs[1] != NULL) {
			fprintf(stderr, "FAIL n1-cdef-dropped: expected NULL\n");
			failures++;
		} else {
			printf("ok   n1-cdef-dropped\n");
		}
		free_gd(g);
	}

	/* Helpers that the render-time path uses to peek into the RRD file. */
	{
		char *fn = def_rrdfile("DEF:p1=tcp.conn-smoke.rrd:ping1:AVERAGE");
		if (!fn || strcmp(fn, "tcp.conn-smoke.rrd") != 0) {
			fprintf(stderr, "FAIL def_rrdfile: got %s\n", fn ? fn : "(null)");
			failures++;
		} else printf("ok   def_rrdfile: %s\n", fn);
		free(fn);
	}
	{
		char *ds = def_dsname("DEF:p1=tcp.conn-smoke.rrd:ping1:AVERAGE");
		if (!ds || strcmp(ds, "ping1") != 0) {
			fprintf(stderr, "FAIL def_dsname: got %s\n", ds ? ds : "(null)");
			failures++;
		} else printf("ok   def_dsname: %s\n", ds);
		free(ds);
	}
	{
		char *pf = dsname_prefix("ping@DSIDX@");
		if (!pf || strcmp(pf, "ping") != 0) {
			fprintf(stderr, "FAIL dsname_prefix: got %s\n", pf ? pf : "(null)");
			failures++;
		} else printf("ok   dsname_prefix: %s\n", pf);
		free(pf);
	}
	{
		char *fn = def_rrdfile("AREA:slice1#FFFFFF");
		if (fn != NULL) {
			fprintf(stderr, "FAIL def_rrdfile-non-DEF: should be NULL, got %s\n", fn);
			failures++; free(fn);
		} else printf("ok   def_rrdfile-non-DEF\n");
	}

	/* expand_dsidx_array doesn't mutate input and handles n<=0 by passing
	 * templates through (the fail-fast/render-time deferred case). */
	{
		char *src[] = { "DEF:p@DSIDX@=x:p@DSIDX@", NULL };
		char **out0 = expand_dsidx_array(src, 0);
		char **out3 = expand_dsidx_array(src, 3);
		if (strcmp(out0[0], "DEF:p@DSIDX@=x:p@DSIDX@") != 0) {
			fprintf(stderr, "FAIL array-n0: %s\n", out0[0]); failures++;
		} else printf("ok   array-n0-passthrough\n");
		if (strcmp(out3[0], "DEF:p1=x:p1") != 0 || strcmp(out3[2], "DEF:p3=x:p3") != 0) {
			fprintf(stderr, "FAIL array-n3\n"); failures++;
		} else printf("ok   array-n3-expand\n");
		/* Original src must be unchanged. */
		if (strcmp(src[0], "DEF:p@DSIDX@=x:p@DSIDX@") != 0) {
			fprintf(stderr, "FAIL array-no-mutation\n"); failures++;
		} else printf("ok   array-no-mutation\n");
		{ int i; for (i = 0; out0[i]; i++) free(out0[i]); free(out0); }
		{ int i; for (i = 0; out3[i]; i++) free(out3[i]); free(out3); }
	}

	if (failures) { fprintf(stderr, "\n%d failure(s)\n", failures); return 1; }
	printf("\nAll tests passed.\n");
	return 0;
}
