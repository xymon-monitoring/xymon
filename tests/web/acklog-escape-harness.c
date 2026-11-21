/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Driver for the two renderers of acknowledge.log, for
 * tests/web/acklog-escape.sh. It loads the hosts.cfg named by argv[1], then
 * writes, from the log at $XYMONSERVERLOGS, the non-green page's ack table
 * (do_acklog(), lib/acklog.c), a line "=====", and acknowledgements.cgi's
 * report (do_acknowledgementslog(), lib/acknowledgementslog.c).
 *
 * Everything runs through the real library; nothing here mirrors the escaping.
 */
#include <stdio.h>
#include <stdlib.h>

#include "libxymon.h"

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s hosts.cfg\n", argv[0]);
		return 2;
	}

	load_hostnames(argv[1], NULL, get_fqdn());
	do_acklog(stdout, 100, 240);
	printf("=====\n");
	do_acknowledgementslog(stdout, 100, 240, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	return 0;
}
