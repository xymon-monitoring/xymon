/*----------------------------------------------------------------------------*/
/* Hobbit ORCA data collector.                                                */
/* This tool grabs the last reading from an ORCA logfile and formats it in    */
/* NAME:VALUE format for the client message.                                  */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: orcahobbit.c,v 1.1 2006-06-20 11:44:59 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	time_t now;
	char *prefix = NULL, *machinename = NULL;
	char datestr[12], fn[PATH_MAX];
	int i;
	FILE *fd;
	char headerline[32768];
	char vals[32768];
	int gotvals = 0;
	char *hp, *hdr, *vp, *val;
	char msgline[4096];
	strbuffer_t *msg;

	machinename = xgetenv("MACHINE");

	for (i=1; (i < argc); i++) {
		if (strncmp(argv[i], "--orca=", 7) == 0) {
			prefix = argv[i]+7;
		}
		else if (strncmp(argv[i], "--machine=", 10) == 0) {
			machinename = argv[i]+10;
		}
		else if (strcmp(argv[i], "--debug") == 0) {
			debug = dontsendmessages = 1;
		}
	}

	if (!prefix || !machinename) return 0;

	/* 
	 * ORCA logfiles are names PREFIX-%Y-%m-%d-XXX where XXX is a 
	 * number starting at 0 and increasing whenever the columns
	 * change.
	 * We will look for the first 20 index numbers only.
	 */
	now = time(NULL);
	strftime(datestr, sizeof(datestr), "%Y-%m-%d", localtime(&now));
	i = 0; fd = NULL;
	while ((i < 20) && !fd) {
		snprintf(fn, sizeof(fn), "%s-%s-%03d", prefix, datestr, i);
		fd = fopen(fn, "r");
	}

	if (!fd) return 1;

	/* Grab the header line, and the last logfile entry. */
	if (fgets(headerline, sizeof(headerline), fd)) {
		while (fgets(vals, sizeof(vals), fd)) gotvals = 1;
	}
	fclose(fd);

	msg = newstrbuffer(0);
	sprintf(msgline, "data %s.orca\n", machinename);
	addtobuffer(msg, msgline);

	/* Match headers and values. */
	hdr = strtok_r(headerline, " \t\n", &hp);
	val = strtok_r(vals, " \t\n", &vp);
	while (hdr && val) {
		sprintf(msgline, "%s:%s\n", hdr, val);
		addtobuffer(msg, msgline);
		hdr = strtok_r(NULL, " \t\n", &hp);
		val = strtok_r(NULL, " \t\n", &vp);
	}

	sendmessage(STRBUF(msg), NULL, NULL, NULL, 0, BBTALK_TIMEOUT);

	return 0;
}

