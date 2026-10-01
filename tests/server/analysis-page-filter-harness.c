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
 * host". Two are used, one from each side of the defect:
 *
 *   load   - get_cpu_thresholds(), which has always read XMH_ALLPAGEPATHS and
 *            is the getter the OS handlers that report a cpu section call
 *            first, so its answer is the one the rest of that report inherits.
 *            Default loadyellow 5.0.
 *   paging - get_paging_thresholds(), one of the getters that used to read
 *            XMH_PAGEPATH, the host's primary pagepath alone. Default 5.
 *
 * ruleset() caches its answer per hostname, so the getter called first for a
 * host fixes the ruleset every later one receives. While the two disagreed
 * about which page item to ask for, that made the answer depend on call order
 * -- which is what the ordered probes pin:
 *
 *   load-after-paging   - paging first, then load, reporting the load value.
 *   paging-after-load   - load first, then paging, reporting the paging value.
 *
 * Each must agree with the matching unordered probe. paging is a stand-in for
 * the whole group: the getters share ruleset(), so one of them reading the
 * wrong item is the defect, and which one it is changes only who notices. The
 * shell script pins the rest of the group at the source instead, since a probe
 * per getter would say nothing new about the mechanism.
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

static double load_probe(void *hinfo)
{
	float loadyellow, loadred;
	int recentlimit, ancientlimit, uptimecolor, maxclockdiff, clockdiffcolor;

	get_cpu_thresholds(hinfo, "", &loadyellow, &loadred,
			   &recentlimit, &ancientlimit, &uptimecolor,
			   &maxclockdiff, &clockdiffcolor);
	return loadyellow;
}

static double paging_probe(void *hinfo)
{
	int pagingyellow, pagingred;

	get_paging_thresholds(hinfo, "", &pagingyellow, &pagingred);
	return pagingyellow;
}

/* argv[1] = hosts.cfg, argv[2] = analysis.cfg, argv[3] = probe, argv[4..] = hosts. */
int main(int argc, char *argv[])
{
	int i;
	char *probe;

	if (argc < 5) {
		fprintf(stderr, "usage: %s hosts.cfg analysis.cfg "
				"load|paging|load-after-paging|paging-after-load host [host...]\n",
			argv[0]);
		return 2;
	}

	probe = argv[3];
	if ((strcmp(probe, "load") != 0) && (strcmp(probe, "paging") != 0) &&
	    (strcmp(probe, "load-after-paging") != 0) && (strcmp(probe, "paging-after-load") != 0)) {
		fprintf(stderr, "unknown probe: %s\n", probe);
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

		if (strcmp(probe, "load") == 0) {
			threshold = load_probe(hinfo);
		}
		else if (strcmp(probe, "paging") == 0) {
			threshold = paging_probe(hinfo);
		}
		else if (strcmp(probe, "load-after-paging") == 0) {
			/* Prime the cache through the other getter, then report this one. */
			paging_probe(hinfo);
			threshold = load_probe(hinfo);
		}
		else {
			load_probe(hinfo);
			threshold = paging_probe(hinfo);
		}

		printf("%s=%s=%.1f\n", argv[i], xmh_item(hinfo, XMH_ALLPAGEPATHS), threshold);
	}

	return 0;
}
