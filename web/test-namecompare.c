/*
 * test-namecompare.c - unit tests for instance_key_compare(), included
 * from the same source showgraph.c uses (namecompare.inc.c).
 *
 * Build & run (from web/): make test-namecompare && ./test-namecompare
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "namecompare.inc.c"

static int failures = 0;

static void order(const char *a, const char *b)
{
	if (instance_key_compare(a, b) >= 0 || instance_key_compare(b, a) <= 0) {
		fprintf(stderr, "FAIL: expected \"%s\" < \"%s\"\n", a, b);
		failures++;
	}
}

static int qcmp(const void *a, const void *b)
{
	return instance_key_compare(*(const char *const *)a, *(const char *const *)b);
}

int main(void)
{
	/* The doc's measured intransitivity: {9, 10, 1a} sorted from every
	 * permutation must give ONE result - the old comparator gave three. */
	const char *want[] = { "1a", "9", "10" };
	const char *perms[6][3] = {
		{"9","10","1a"}, {"9","1a","10"}, {"10","9","1a"},
		{"10","1a","9"}, {"1a","9","10"}, {"1a","10","9"}
	};
	int p, i;

	for (p = 0; p < 6; p++) {
		const char *v[3];
		for (i = 0; i < 3; i++) v[i] = perms[p][i];
		qsort(v, 3, sizeof(v[0]), qcmp);
		for (i = 0; i < 3; i++) {
			if (strcmp(v[i], want[i])) {
				fprintf(stderr, "FAIL: permutation %d sorted to {%s,%s,%s}\n", p, v[0], v[1], v[2]);
				failures++;
				break;
			}
		}
	}

	/* Distinct keys never compare equal ("007" vs "7" was 0 before) */
	if (instance_key_compare("007", "7") == 0) { fprintf(stderr, "FAIL: 007 == 7\n"); failures++; }
	if (instance_key_compare("007", "7") != -instance_key_compare("7", "007")) {
		fprintf(stderr, "FAIL: 007/7 asymmetric\n"); failures++;
	}

	/* OID/version components sort numerically per component */
	order("1.2.1", "1.10.1");
	order("1.9.9", "1.10.0");
	order("2", "10");
	order("eth2", "eth10");

	/* plain names keep byte order; prefixes sort first */
	order("alpha", "beta");
	order("eth1", "eth1x");

	/* a leading '-' shared by both keys is a sign, as in the old
	 * strtol()-based sort: larger magnitude sorts first */
	order("-5", "-3");
	order("-10", "-9");
	order("-10", "-09");
	order("-2", "1");
	order("-5", "0");

	/* an embedded '-' is an ordinary byte (the old code fell back to
	 * strcmp() for such keys); the run after it compares unsigned */
	order("temp-3", "temp-5");
	order("temp-9", "temp-10");

	/* leading-zero negatives are distinct and antisymmetric */
	if (instance_key_compare("-007", "-7") == 0) { fprintf(stderr, "FAIL: -007 == -7\n"); failures++; }
	if (instance_key_compare("-007", "-7") != -instance_key_compare("-7", "-007")) {
		fprintf(stderr, "FAIL: -007/-7 asymmetric\n"); failures++;
	}

	/* equal keys are equal */
	if (instance_key_compare("1.2.3", "1.2.3") != 0) { fprintf(stderr, "FAIL: reflexivity\n"); failures++; }

	printf(failures ? "FAILED\n" : "All tests passed.\n");
	return failures ? 1 : 0;
}
