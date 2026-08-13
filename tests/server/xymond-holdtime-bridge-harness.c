/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * Behavioural half of tests/server/xymond-holdtime-bridge.sh.
 *
 * When a test stops reporting, xymond invents a status for it and the real
 * one is lost from view. If the test comes back wearing the colour it left
 * with, the problem plainly did not go away while we were blind, so the
 * hold-time carries across the silence -- which is what alerts.cfg DURATION
 * rules read.
 *
 * Two things bound that inference, and this drives both:
 *
 *   - the colour must match. Coming back a different colour means something
 *     did happen while we could not see it, so the clock starts fresh.
 *   - the silence must be short. Nothing caps how long a status may sit
 *     stale, so without a limit a host absent for a month would return
 *     claiming a month-long hold-time and page at full escalation the moment
 *     it reappeared.
 *
 * The companion shell test prepends xymond/xymond.c's real
 * GAPBRIDGE_VALIDITIES and holdtime_bridges() when it builds this harness.
 */
static int failures = 0;

static void expect(const char *label, int got, int want)
{
	if (got != want) {
		fprintf(stderr, "%s: expected %s, got %s\n", label,
			(want ? "bridge" : "fresh hold-time"),
			(got ? "bridge" : "fresh hold-time"));
		failures++;
	}
}

int main(void)
{
	const int validity = 30;			/* minutes; xymond's default */
	const time_t limit = GAPBRIDGE_VALIDITIES * validity * 60;

	/* No gap at all: an ordinary colour change starts a fresh hold-time. */
	expect("no gap recorded",
	       holdtime_bridges(COL_RED, NO_COLOR, 0, validity), 0);

	/* Came back as it left, well inside the limit: bridge. */
	expect("same colour, brief silence",
	       holdtime_bridges(COL_RED, COL_RED, 60, validity), 1);

	/* Came back different: something moved while we were blind. */
	expect("colour changed during the silence",
	       holdtime_bridges(COL_GREEN, COL_RED, 60, validity), 0);

	/* The bound, from both sides and exactly on it. */
	expect("silence just under the limit",
	       holdtime_bridges(COL_RED, COL_RED, limit - 1, validity), 1);
	expect("silence exactly at the limit",
	       holdtime_bridges(COL_RED, COL_RED, limit, validity), 1);
	expect("silence just over the limit",
	       holdtime_bridges(COL_RED, COL_RED, limit + 1, validity), 0);

	/* The case the bound exists for: gone a month, back red. */
	expect("gone a month, back the same colour",
	       holdtime_bridges(COL_RED, COL_RED, 30*24*3600, validity), 0);

	/*
	 * The bound scales with how often the test reports, so a test that
	 * checks in rarely is allowed a proportionally longer silence.
	 */
	expect("slow test, silence inside its own limit",
	       holdtime_bridges(COL_RED, COL_RED, 10*3600, 240), 1);
	expect("fast test, same silence is far outside its limit",
	       holdtime_bridges(COL_RED, COL_RED, 10*3600, 5), 0);

	/*
	 * The bound must be computed at time_t width. validity is whatever the
	 * reporter asked for with "status+NNN", and 4 * 10000000 * 60 overflows a
	 * 32-bit int to a negative number -- which silently refuses to bridge even
	 * a one-minute gap for that test.
	 */
	expect("huge validity, brief silence",
	       holdtime_bridges(COL_RED, COL_RED, 60, 10000000), 1);

	/* Every colour bridges, not just purple -- clear and blue gaps too. */
	expect("green held across a clear gap",
	       holdtime_bridges(COL_GREEN, COL_GREEN, 60, validity), 1);
	expect("yellow held across a gap",
	       holdtime_bridges(COL_YELLOW, COL_YELLOW, 60, validity), 1);

	if (failures) {
		fprintf(stderr, "%d hold-time bridge assertion(s) failed\n", failures);
		return 1;
	}

	return 0;
}
