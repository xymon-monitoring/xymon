/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * Behavioural half of tests/server/xymond-flap-purple-pin.sh.
 *
 * While a test is flapping, xymond holds it at the most critical level it
 * reported instead of following every oscillation. Purple is not a level --
 * it means "no data" -- so it must not take part in that comparison in either
 * direction.
 *
 * The direction this guards is purple as the RECORDED color. Purple (3)
 * outranks green (0), clear (1) and blue (2), so a rule that only exempts the
 * incoming color still rewrites every report from a resumed test back to
 * purple, and holds it there for the rest of the flap window -- displaying and
 * (purple being an alert color by default) paging "no data" for a test that is
 * actively reporting.
 *
 * The companion shell test prepends xymond/xymond.c's real static
 * flap_pinned_color() when it builds this harness; this file only drives it.
 */
static int failures = 0;

static const char *cname(int c)
{
	switch (c) {
	  case COL_GREEN:  return "green";
	  case COL_CLEAR:  return "clear";
	  case COL_BLUE:   return "blue";
	  case COL_PURPLE: return "purple";
	  case COL_YELLOW: return "yellow";
	  case COL_RED:    return "red";
	}
	return "?";
}

static void expect(int newcolor, int logcolor, int want)
{
	int got = flap_pinned_color(newcolor, logcolor);

	if (got != want) {
		fprintf(stderr, "incoming=%s recorded=%s: expected %s, got %s\n",
			cname(newcolor), cname(logcolor), cname(want), cname(got));
		failures++;
	}
}

int main(void)
{
	int levels[] = { COL_GREEN, COL_CLEAR, COL_BLUE, COL_YELLOW, COL_RED };
	int i, j;

	/*
	 * The regression: a test recorded purple that reports again keeps the
	 * color it actually sent, whatever that is.
	 */
	for (i = 0; i < (int)(sizeof(levels)/sizeof(levels[0])); i++)
		expect(levels[i], COL_PURPLE, levels[i]);

	/* Purple arriving is recorded as purple, never pinned up to the old level. */
	for (i = 0; i < (int)(sizeof(levels)/sizeof(levels[0])); i++)
		expect(COL_PURPLE, levels[i], COL_PURPLE);

	/* Purple on both sides stays purple. */
	expect(COL_PURPLE, COL_PURPLE, COL_PURPLE);

	/*
	 * Everything else is unchanged: between two real levels the more
	 * critical one wins, whichever side it is on.
	 */
	for (i = 0; i < (int)(sizeof(levels)/sizeof(levels[0])); i++) {
		for (j = 0; j < (int)(sizeof(levels)/sizeof(levels[0])); j++) {
			int newcolor = levels[i], logcolor = levels[j];
			int want = (newcolor < logcolor) ? logcolor : newcolor;

			expect(newcolor, logcolor, want);
		}
	}

	/* A worked example of the reported failure, spelled out. */
	expect(COL_GREEN, COL_PURPLE, COL_GREEN);

	if (failures) {
		fprintf(stderr, "%d flap-pin assertion(s) failed\n", failures);
		return 1;
	}

	return 0;
}
