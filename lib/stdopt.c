/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains miscellaneous routines.                                        */
/*                                                                            */
/* Copyright (C) 2002-2012 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: stdopt.c 7008 2012-06-02 09:22:02Z storner $";

#include <string.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#include "libxymon.h"

char *programtoolname = NULL;
char *programpath = NULL;
char *programname = NULL;
char *pidfn = NULL;
char *hostsfn = NULL;
char *logfn = NULL;
char *envarea = NULL;
int  showhelp = 0;
int  dontsendmessages = 0;
ipprotocol_t ipprotocol = XYMON_IPPROTO_ANY;

int standardoption(char *opt)
{
	if (!opt) return 0;

	if (strcmp(opt, "--debug") == 0) {
		debug = 1;
	}
	else if (strcmp(opt, "--no-update") == 0) {
		dontsendmessages = 1;
	}
	else if (strcmp(opt, "--no-update-brief") == 0) {
		dontsendmessages = 2;
	}
	else if (argnmatch(opt, "--env=")) {
		char *p = strchr(opt, '=');
		loadenv(p+1, envarea);
		/* Immediately load any remaining defaults */
		xymon_default_xymonhome(programtoolname);
	}
	else if (argnmatch(opt, "--area=")) {
		char *p = strchr(opt, '=');
		envarea = strdup(p+1);
	}
	else if (argnmatch(opt, "--hosts=")) {
		char *p = strchr(opt, '=');
		hostsfn = strdup(p+1);
	}
	else if (argnmatch(opt, "--log=") || argnmatch(opt, "--logfile=")) {
		char *p = strchr(opt, '=');
		logfn = strdup(p+1);
	}
	else if (argnmatch(opt, "--pidfile") || argnmatch(opt, "--pid")) {
		char *p = strchr(opt, '=');
		if (pidfn) free(pidfn);
		if (p && *(p+1)) pidfn = strdup(p+1);
		else {
			pidfn = (char *)malloc(strlen(xgetenv("XYMONRUNDIR")) + strlen(programname) + 6);
			sprintf(pidfn, "%s/%s.pid", xgetenv("XYMONRUNDIR"), programname);
		}
	}
	else if ((strcmp(opt, "--help") == 0) || (strcmp(opt, "-?") == 0)) {
		fprintf(stdout, "%s %s\n", programname, VERSION);
		showhelp = 1;
	}
	else if (strcmp(opt, "--version") == 0) {
		fprintf(stdout, "%s %s\n", programname, VERSION);
		exit(0);
	}
	else if ((strcmp(opt, "-4") == 0) || (strcmp(opt, "--4") == 0)) {
		ipprotocol = XYMON_IPPROTO_4;
	}
	else if ((strcmp(opt, "-6") == 0) || (strcmp(opt, "--6") == 0)) {
		ipprotocol = XYMON_IPPROTO_6;
	}
	else {
		return 0;
	}

	return 1;
}

void libxymon_init(char *toolname)
{
	static int firstcall = 1;

	if (firstcall) {
		firstcall = 0;

		if (getenv("EARLYDEBUG")) debug = 1;	/* For debugging before commandline options are parsed */

		programtoolname = strdup(toolname);
		programname = basename(strdup(toolname));
		programpath = dirname(strdup(toolname));

		setup_signalhandler(programname);
	}
}

