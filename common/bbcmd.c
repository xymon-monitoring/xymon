/*----------------------------------------------------------------------------*/
/* Big Brother application launcher.                                          */
/*                                                                            */
/* This is used to launch a single BB application, with the environment that  */
/* would normally be established by hobbitlaunch.                             */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbcmd.c,v 1.4 2005-01-15 17:39:19 henrik Exp $";

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	int argi;
	char *cmd = NULL;
	char **cmdargs = NULL;
	int argcount = 0;

	cmdargs = (char **) xcalloc(argc+2, sizeof(char *));
	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
		else {
			if (argcount == 0) {
				cmdargs[0] = cmd = xstrdup(expand_env(argv[argi]));
				argcount = 1;
			}
			else cmdargs[argcount++] = xstrdup(expand_env(argv[argi]));
		}
	}

	/* Go! */
	execvp(cmd, cmdargs);

	/* Should never go here */
	errprintf("execvp() failed: %s\n", strerror(errno));

	return 0;
}

