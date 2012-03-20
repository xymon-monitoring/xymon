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

static char rcsid[] = "$Id: misc.c 6895 2012-01-23 09:14:44Z storner $";

#include <string.h>
#include <stdio.h>

#include "libxymon.h"

char *programname = NULL;
char *pidfn = NULL;
char *hostsfn = NULL;
char *logfn = NULL;
char *envarea = NULL;
int  showhelp = 0;
int  dontsendmessages = 0;


int standardoption(char *id, char *opt)
{
	static int firstcall = 1;

	if (firstcall) {
		firstcall = 0;

		programname = basename(strdup(id));

		pidfn = (char *)malloc(strlen(xgetenv("XYMONSERVERLOGS")) + strlen(programname) + 6);
		sprintf(pidfn, "%s/%s.pid", xgetenv("XYMONSERVERLOGS"), programname);
	}

	if (strcmp(opt, "--debug") == 0) {
		debug = 1;
	}
	else if (strcmp(opt, "--no-update") == 0) {
		dontsendmessages = 1;
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
		char *progname = strdup(id);
		fprintf(stderr, "%s %s\n", basename(progname), VERSION);
		xfree(progname);
		showhelp = 1;
	}
	else {
		return 0;
	}

	return 1;
}

