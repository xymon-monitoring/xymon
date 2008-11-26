/*----------------------------------------------------------------------------*/
/* Hobbit utility to convert the deprecated NK tags to a hobbit-nkview.cfg    */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: convertnk.c,v 1.1 2006/09/12 21:27:11 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	namelist_t *walk;

	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());

	for (walk = first_host(); (walk); walk=walk->next) {
		char *nk, *nktime, *tok;

		nk = bbh_item(walk, BBH_NK); if (!nk) continue;
		nktime = bbh_item(walk, BBH_NKTIME);

		nk = strdup(nk);

		tok = strtok(nk, ",");
		while (tok) {
			char *hostname = bbh_item(walk, BBH_HOSTNAME);
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

