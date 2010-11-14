/*----------------------------------------------------------------------------*/
/* Xymon utility to convert the deprecated NK tags to a critical.cfg          */
/*                                                                            */
/* Copyright (C) 2006-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "libxymon.h"

int main(int argc, char *argv[])
{
	void *walk;

	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());

	for (walk = first_host(); (walk); walk=next_host(walk, 0)) {
		char *nk, *nktime, *tok;

		nk = xmh_item(walk, XMH_NK); if (!nk) continue;
		nktime = xmh_item(walk, XMH_NKTIME);

		nk = strdup(nk);

		tok = strtok(nk, ",");
		while (tok) {
			char *hostname = xmh_item(walk, XMH_HOSTNAME);
			char *startstr = "", *endstr = "", *ttgroup = "", *ttextra = "", *updinfo = "Migrated";
			int priority = 2;

			fprintf(stdout, "%s|%s|%s|%s|%s|%d|%s|%s|%s\n",
				hostname, tok,
				startstr, endstr,
				(nktime ? nktime : ""),
				priority, ttgroup, ttextra, updinfo);

			tok = strtok(NULL, ",");
		}

		xfree(nk);
	}

	return 0;
}

