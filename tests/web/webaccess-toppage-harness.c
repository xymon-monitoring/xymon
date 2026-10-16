/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/web/webaccess-toppage-harness.c
 *
 * Driver for web_access_allowed() (lib/webaccess.c), used by
 * webaccess-toppage.sh -- issue #535.
 *
 * The function grants access by page name: it walks the host's pagepath list,
 * reduces each element to its top-level component, and looks up
 * "<component> <username>" in the tree built from the access config. What is
 * under test is which hosts a given group line can reach, so the harness takes
 * a hosts.cfg, an access config, and a host/user pair, and prints the verdict.
 *
 * Prints "<host>=<pagepaths>=<0|1>". The scenarios and the expected verdicts
 * live in the shell script, which owns the pass/fail decision.
 *
 * usage: harness <hosts.cfg> <access.cfg> <user> <host> [host...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libxymon.h"

int main(int argc, char *argv[])
{
	int i;

	if (argc < 5) {
		fprintf(stderr, "usage: %s hosts.cfg access.cfg user host [host...]\n", argv[0]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());
	if (load_web_access_config(argv[2]) == NULL) {
		fprintf(stderr, "cannot load %s\n", argv[2]);
		return 2;
	}

	for (i = 4; i < argc; i++) {
		void *hinfo = hostinfo(argv[i]);

		char *paths;
		int allowed;

		if (!hinfo) { fprintf(stderr, "no such host: %s\n", argv[i]); return 2; }

		/*
		 * Taken in order, not as two printf arguments: xmh_item() returns a
		 * pointer into a static strbuffer that web_access_allowed() clears and
		 * refills by calling xmh_item() itself, and the order in which printf's
		 * arguments are evaluated is unspecified.
		 */
		paths = strdup(xmh_item(hinfo, XMH_ALLPAGEPATHS));
		allowed = web_access_allowed(argv[3], argv[i], NULL, WEB_ACCESS_VIEW);

		printf("%s=%s=%d\n", argv[i], paths, allowed);
		free(paths);
	}

	return 0;
}
