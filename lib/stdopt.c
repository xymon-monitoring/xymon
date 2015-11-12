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

#include "libxymon.h"

char *programname = NULL;
char *pidfn = NULL;
char *hostsfn = NULL;
char *logfn = NULL;
char *envarea = NULL;
int  showhelp = 0;
int  dontsendmessages = 0;


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
	else if (argnmatch(opt, "--pidfile=") || argnmatch(opt, "--pid=")) {
		char *p = strchr(opt, '=');
		pidfn = strdup(p+1);
	}
	else if ((strcmp(opt, "--version") == 0) || (strcmp(opt, "--help") == 0) || (strcmp(opt, "-?") == 0)) {
		fprintf(stderr, "%s %s\n", programname, VERSION);
		showhelp = 1;
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

		programname = basename(strdup(toolname));

		pidfn = (char *)malloc(strlen(xgetenv("XYMONSERVERLOGS")) + strlen(programname) + 6);
		sprintf(pidfn, "%s/%s.pid", xgetenv("XYMONSERVERLOGS"), programname);

		setup_signalhandler(programname);
	}
}

