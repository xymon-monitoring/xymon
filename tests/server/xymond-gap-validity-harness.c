/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * tests/server/xymond-gap-validity-harness.c
 *
 * Appended to the real gap_within_window(), holdtime_bridges() and
 * update_gapstate() extracted from xymond/xymond.c. Only the log fields the
 * extracted code touches are declared, and the clock is supplied by the
 * caller: reaching this through a live daemon means waiting out a report
 * validity, which a test cannot do.
 */

static int failures = 0;

static void check(const char *what, int want, int got)
{
	if (want == got) printf("ok   %s\n", what);
	else { printf("FAIL %s: want %d, got %d\n", what, want, got); failures++; }
}

/* One silence and one return.
 *
 *   opencolor    the colour on record when the test fell silent
 *   openvalidity what it was promising then, in minutes
 *   gaplen       how long the silence lasted, in seconds
 *   backvalidity what the returning report promises
 *   backcolor    the colour it comes back with
 *
 * Returns 1 when the hold-time carried across the gap. */
static int silence_and_return(int opencolor, int openvalidity, time_t gaplen,
			      int backvalidity, int backcolor)
{
	xymond_log_t log;
	time_t t0 = 1786000000;
	time_t heldsince = t0 - 3600;

	memset(&log, 0, sizeof(log));
	log.pregapcolor = NO_COLOR;
	log.oldcolor = opencolor;
	log.lastchange = heldsince;
	log.validity = openvalidity;

	/* xymond invents a colour because nothing arrived: the gap opens, and it
	 * is the validity in force at that moment that sizes the window. */
	update_gapstate(&log, COL_PURPLE, 1, 1, t0, heldsince, openvalidity);

	/* The test comes back with its own colour, and its own promise. */
	log.validity = backvalidity;
	log.oldcolor = COL_PURPLE;
	log.lastchange = t0 + gaplen;
	update_gapstate(&log, backcolor, 0, 0, t0 + gaplen, t0 + gaplen, backvalidity);

	return (log.lastchange == heldsince);
}

int main(void)
{
	time_t onehour = 3600;

	/* Reporting every five minutes, silent for an hour, back announcing four
	 * hours. The window belongs to the five-minute regime -- twenty minutes
	 * with GAPBRIDGE_VALIDITIES at 4 -- so an hour is far past it. Sized from
	 * the returning report instead, sixteen hours would have swallowed it. */
	check("a 1h gap opened at status+5 is not bridged by a returning status+240",
	      0, silence_and_return(COL_RED, 5, onehour, 240, COL_RED));

	/* And the reverse: the test was promising four hours when it fell silent,
	 * so an hour of silence is well inside its window, whatever the returning
	 * report promises. Sized from the return, five minutes would have
	 * rejected it. */
	check("a 1h gap opened at status+240 is bridged although the return says status+5",
	      1, silence_and_return(COL_RED, 240, onehour, 5, COL_RED));

	/* The rule itself still holds on both sides of its own window. */
	check("a 10m gap opened at status+5 is bridged",
	      1, silence_and_return(COL_RED, 5, 600, 5, COL_RED));
	check("a colour that changed across the gap is never bridged",
	      0, silence_and_return(COL_RED, 240, onehour, 240, COL_GREEN));

	if (failures) { printf("%d check(s) FAILED\n", failures); return 1; }
	printf("all gap-validity checks ok\n");
	return 0;
}
