/* threshold_eval: evaluate a metric value against a declared THRESHOLD spec
 * ("<ds>:<relop><operand>[:<sev>][,...]") to the worst firing severity. The
 * shared engine used by the status table and (RFC #218) the alert path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "threshold.h"

static int failures = 0;

static void expect(const char *label, int got, int want)
{
	if (got != want) {
		fprintf(stderr, "%s: got %d, wanted %d\n", label, got, want);
		failures++;
	}
}

/* Resolves a DS-name operand to a value, for DS-vs-DS thresholds. */
static int lookup(const char *ds, double *out, void *ud)
{
	(void)ud;
	if (strcmp(ds, "spare") == 0) { *out = 8.0;  return 1; }
	if (strcmp(ds, "limit") == 0) { *out = 50.0; return 1; }
	return 0;	/* unknown DS */
}

int main(void)
{
	const char *T = "temp:>55:warn,temp:>65:crit";

	/* Numeric thresholds and warn->crit escalation. */
	expect("below all -> ok",          threshold_eval(T, "temp", 38, NULL, NULL), THRESHOLD_OK);
	expect("warn band",                threshold_eval(T, "temp", 58, NULL, NULL), THRESHOLD_WARN);
	expect("crit beats warn",          threshold_eval(T, "temp", 68, NULL, NULL), THRESHOLD_CRIT);
	expect("'>' is strict at limit",   threshold_eval(T, "temp", 55, NULL, NULL), THRESHOLD_OK);

	/* Each relop. */
	expect(">= fires at boundary",     threshold_eval("t:>=55:crit", "t", 55, NULL, NULL), THRESHOLD_CRIT);
	expect("< fires below",            threshold_eval("t:<10:crit",  "t", 8,  NULL, NULL), THRESHOLD_CRIT);
	expect("<= fires at boundary",     threshold_eval("t:<=10:warn", "t", 10, NULL, NULL), THRESHOLD_WARN);
	expect("< strict at boundary",     threshold_eval("t:<10:crit",  "t", 10, NULL, NULL), THRESHOLD_OK);

	/* Severity defaults to crit when omitted. */
	expect("default severity = crit",  threshold_eval("t:>5", "t", 9, NULL, NULL), THRESHOLD_CRIT);

	/* Only rules whose base DS matches apply. */
	expect("rule on another DS ignored", threshold_eval("other:>1:crit", "temp", 100, NULL, NULL), THRESHOLD_OK);

	/* DS-vs-DS operands. */
	expect("DS operand fires (20>8)",  threshold_eval("used:>spare:crit", "used", 20, lookup, NULL), THRESHOLD_CRIT);
	expect("DS operand not met",       threshold_eval("used:<limit:warn", "used", 60, lookup, NULL), THRESHOLD_OK);
	expect("DS operand unresolvable",  threshold_eval("used:>ghost:crit", "used", 100, lookup, NULL), THRESHOLD_OK);
	expect("DS operand, no getval",    threshold_eval("used:>spare:crit", "used", 20, NULL, NULL), THRESHOLD_OK);

	/* Malformed / safety - never fatal. */
	expect("null spec",                threshold_eval(NULL, "t", 5, NULL, NULL), THRESHOLD_OK);
	expect("no colon",                 threshold_eval("garbage", "garbage", 5, NULL, NULL), THRESHOLD_OK);
	expect("empty operand",            threshold_eval("t:>:crit", "t", 5, NULL, NULL), THRESHOLD_OK);
	expect("empty base vs real DS",    threshold_eval(":>5:crit", "temp", 9, NULL, NULL), THRESHOLD_OK);

	if (failures) {
		fprintf(stderr, "%d threshold assertion(s) failed\n", failures);
		return 1;
	}
	return 0;
}
