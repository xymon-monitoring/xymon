/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/server/analysis-page-filter-harness.c
 *
 * Driver for the PAGE=/EXPAGE= host filters in analysis.cfg, i.e. ruleset()
 * in xymond/client_config.c, used by analysis-page-filter.sh.
 *
 * ruleset() decides which rules apply to a host and is static, so the probe
 * goes through a public caller whose threshold is a scalar a PAGE=-qualified
 * rule can set and which has a known default meaning "no rule reached this
 * host". Two are offered, because they do not ask the same question:
 *
 *   load   - get_cpu_thresholds(), which passes XMH_ALLPAGEPATHS, the full
 *            list of pages a host is on. Default loadyellow 5.0.
 *   paging - get_paging_thresholds(), which passes XMH_PAGEPATH, the host's
 *            primary pagepath alone and still the empty string on the front
 *            page. This is what reaches ruleset()'s own "/" naming; through
 *            the load probe that naming is unreachable, because
 *            XMH_ALLPAGEPATHS already supplies the name. Default 5.
 *
 * ruleset() caches its answer per hostname, so the first getter called for a
 * host fixes the ruleset for every later one. Each run therefore calls exactly
 * one of the two.
 *
 * Prints "<host>=<pagepaths>=<threshold>" for each host named on argv. The
 * scenarios and the expected values live in the shell script, which owns the
 * pass/fail decision.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libxymon.h"
#include "client_config.h"

/* argv[1] = hosts.cfg, argv[2] = analysis.cfg, argv[3] = probe, argv[4..] = hosts. */
int main(int argc, char *argv[])
{
	int i, useload;

	if (argc < 5) {
		fprintf(stderr, "usage: %s hosts.cfg analysis.cfg load|paging host [host...]\n", argv[0]);
		return 2;
	}

	useload = (strcmp(argv[3], "load") == 0);
	if (!useload && (strcmp(argv[3], "paging") != 0)) {
		fprintf(stderr, "unknown probe: %s\n", argv[3]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());

	/*
	 * A fixture that does not parse installs no rule at all, and every host
	 * then reports the built-in default - which is what several of the shell
	 * script's assertions expect to see, so they would pass on nothing.
	 */
	if (!load_client_config(argv[2])) {
		fprintf(stderr, "cannot load %s\n", argv[2]);
		return 2;
	}

	for (i = 4; i < argc; i++) {
		void *hinfo = hostinfo(argv[i]);
		double threshold;

		if (!hinfo) {
			fprintf(stderr, "no such host: %s\n", argv[i]);
			return 2;
		}

		if (useload) {
			float loadyellow, loadred;
			int recentlimit, ancientlimit, uptimecolor, maxclockdiff, clockdiffcolor;

			get_cpu_thresholds(hinfo, "", &loadyellow, &loadred,
					   &recentlimit, &ancientlimit, &uptimecolor,
					   &maxclockdiff, &clockdiffcolor);
			threshold = loadyellow;
		}
		else {
			int pagingyellow, pagingred;

			get_paging_thresholds(hinfo, "", &pagingyellow, &pagingred);
			threshold = pagingyellow;
		}

		printf("%s=%s=%.1f\n", argv[i], xmh_item(hinfo, XMH_ALLPAGEPATHS), threshold);
	}

	return 0;
}
