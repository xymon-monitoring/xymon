/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Driver for do_acklog() (lib/acklog.c). It loads the hosts.cfg named by
 * argv[1], then renders the acknowledgement log at $XYMONSERVERLOGS into the
 * non-green-page ack table on stdout. The caller feeds an acknowledge.log
 * whose message is long enough to hit the 30-byte summary cut, and checks
 * that the cut did not split a UTF-8 character.
 *
 * Everything runs through the real library; nothing here mirrors the cut.
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
	return 0;
}
