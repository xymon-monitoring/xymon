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

static char rcsid[] = "$Id: bbcmd.c,v 1.14 2005-11-18 06:47:21 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
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
	char *envfile = NULL;
	char *envarea = NULL;
	char envfn[PATH_MAX];

	cmdargs = (char **) calloc(argc+2, sizeof(char *));
	for (argi=1; (argi < argc); argi++) {
		if ((argcount == 0) && (strcmp(argv[argi], "--debug") == 0)) {
			debug = 1;
		}
		else if ((argcount == 0) && (argnmatch(argv[argi], "--env="))) {
			char *p = strchr(argv[argi], '=');
			envfile = strdup(p+1);
		}
		else if ((argcount == 0) && (argnmatch(argv[argi], "--area="))) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if ((argcount == 0) && (strcmp(argv[argi], "--version") == 0)) {
			fprintf(stdout, "Hobbit version %s\n", VERSION);
			return 0;
		}
		else {
			if (argcount == 0) {
				cmdargs[0] = cmd = strdup(expand_env(argv[argi]));
				argcount = 1;
			}
			else cmdargs[argcount++] = strdup(expand_env(argv[argi]));
		}
	}

	if (!envfile) {
		struct stat st;

		sprintf(envfn, "%s/etc/hobbitserver.cfg", xgetenv("BBHOME"));
		if (stat(envfn, &st) == -1) sprintf(envfn, "%s/etc/hobbitclient.cfg", xgetenv("BBHOME"));
		errprintf("Using default environment file %s\n", envfn);
		loadenv(envfn, envarea);
	}
	else {
		loadenv(envfile, envarea);
	}

	/* Make sure MACHINE is setup for our child */
	xgetenv("MACHINE");

	/* Go! */
	if (cmd == NULL) cmd = cmdargs[0] = "/bin/sh";
	execvp(cmd, cmdargs);

	/* Should never go here */
	errprintf("execvp() failed: %s\n", strerror(errno));

	return 0;
}

