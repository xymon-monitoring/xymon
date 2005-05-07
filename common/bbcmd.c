/*----------------------------------------------------------------------------*/
/* Hobbit application launcher.                                               */
/*                                                                            */
/* This is used to launch a single Hobbit application, with the environment   */
/* that would normally be established by hobbitlaunch.                        */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbcmd.c,v 1.9 2005-05-07 07:00:56 henrik Exp $";

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	int argi;
	char *cmd = NULL;
	char **cmdargs = NULL;
	int argcount = 0;
	int haveenv = 0;

	cmdargs = (char **) calloc(argc+2, sizeof(char *));
	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, NULL);
			haveenv=1;
		}
		else {
			if (argcount == 0) {
				cmdargs[0] = cmd = strdup(expand_env(argv[argi]));
				argcount = 1;
			}
			else cmdargs[argcount++] = strdup(expand_env(argv[argi]));
		}
	}

	if (!haveenv) {
		char defenvfn[PATH_MAX];

		sprintf(defenvfn, "%s/etc/hobbitserver.cfg", xgetenv("BBHOME"));
		errprintf("Using default environment file %s\n", defenvfn);
		loadenv(defenvfn);
	}

	/* Go! */
	if (cmd == NULL) cmd = cmdargs[0] = "/bin/sh";
	execvp(cmd, cmdargs);

	/* Should never go here */
	errprintf("execvp() failed: %s\n", strerror(errno));

	return 0;
}

