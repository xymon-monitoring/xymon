/*----------------------------------------------------------------------------*/
/* Xymon application launcher.                                                */
/*                                                                            */
/* This is used to launch a single Xymon application, with the environment    */
/* that would normally be established by xymonlaunch.                         */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "libxymon.h"


int main(int argc, char *argv[])
{
	int argi;
	char *cmd = NULL;
	char **cmdargs = NULL;
	int argcount = 0;

	libxymon_init(argv[0]);
	cmdargs = (char **) calloc(argc+2, sizeof(char *));
	for (argi=1; (argi < argc); argi++) {
		/* Process standard args only until we've seen the command - after that, it is standard args for the spawned command! */
		if ((argcount == 0) && (standardoption(argv[argi]))) {
			if (showhelp) return 0;
		}
		else {
			if (argcount == 0) {
				/* Load our default environment, if --env= wasn't given and handled by standardoption() */
				(void)loaddefaultenv();
				cmdargs[0] = cmd = strdup(expand_env(argv[argi]));
				argcount = 1;
			}
			else cmdargs[argcount++] = strdup(expand_env(argv[argi]));
		}
	}

	/* Go! */
	if (cmd == NULL) {
		/* Load our default environment, if --env= wasn't given and handled by standardoption() */
		if (!loaddefaultenv()) xymon_default_xymonhome(argv[0]);
		printf("xymoncmd: No command given; dropping you to a shell\n\n");

		cmd = cmdargs[0] = xgetenv("SHELL");
		if (*cmd == '\0') cmd = cmdargs[0] = "/bin/sh";
		cmdargs[1] = "-i"; cmdargs[2] = NULL;
	}
	execvp(cmd, cmdargs);

	/* Should never go here */
	errprintf("xymoncmd: execvp of '%s' failed: %s\n", cmd, strerror(errno));

	exit(127);
}

