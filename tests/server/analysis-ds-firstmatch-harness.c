/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/server/analysis-ds-firstmatch-harness.c
 *
 * Driver for check_rrdds_thresholds() (xymond/client_config.c), used by
 * analysis-ds-firstmatch.sh to exercise the analysis.cfg first-match rule for
 * DS (RRD dataset) thresholds -- issue #32, fixed in commit 999c3ee03.
 *
 * check_rrdds_thresholds() takes the RRD's dataset-name tree (valnames) and a
 * colon-separated value string; for every DS rule that matches the RRD key it
 * may emit a "modify <host>.<column> <color> rrdds ..." line. The fix makes a
 * rule shadow every later rule sharing its (column, dataset, colour) target,
 * so we probe it by counting "modify" lines for a given RRD key.
 *
 * The harness builds a one-dataset value tree, loads the hosts.cfg and
 * analysis.cfg passed on argv, and prints "<key>=<modify-count>" for each RRD
 * key it is asked about. The scenarios and their expected counts live in the
 * shell script, which owns the pass/fail decision.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libxymon.h"

extern int load_client_config(char *configfn);
extern strbuffer_t *check_rrdds_thresholds(char *hostname, char *classname,
	char *pagepaths, char *rrdkey, void *valnames, char *vals);

/* A value tree with a single dataset DSNAM at index 0 of the value string. */
static void *make_valnames(const char *dsnam)
{
	void *tree = xtreeNew(strcasecmp);
	rrdtplnames_t *n = calloc(1, sizeof(rrdtplnames_t));

	n->dsnam = strdup(dsnam);
	n->idx = 0;
	xtreeAdd(tree, n->dsnam, n);
	return tree;
}

static int count_modify(strbuffer_t *b)
{
	int c = 0;
	char *p = b ? STRBUF(b) : NULL;

	while (p && (p = strstr(p, "modify "))) { c++; p += 7; }
	return c;
}

/*
 * argv[1] = hosts.cfg, argv[2] = analysis.cfg, argv[3] = dataset name,
 * argv[4] = single value, argv[5..] = RRD keys to probe.
 */
int main(int argc, char *argv[])
{
	void *valnames;
	int i;

	if (argc < 6) {
		fprintf(stderr, "usage: %s hosts.cfg analysis.cfg dsname value key [key...]\n", argv[0]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());
	load_client_config(argv[2]);
	valnames = make_valnames(argv[3]);

	for (i = 5; i < argc; i++) {
		strbuffer_t *r = check_rrdds_thresholds("testhost", "", "", argv[i], valnames, argv[4]);
		printf("%s=%d\n", argv[i], count_modify(r));
	}

	return 0;
}
