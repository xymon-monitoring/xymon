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

static void xymon_default_envs(char *envfn)
{
	FILE *fd;
	char buf[1024];
	char *evar;
	char *homedir, *p;

	if (getenv("MACHINEDOTS") == NULL) {
	    if (getenv("HOSTNAME") != NULL) sprintf(buf, "%s", xgetenv("HOSTNAME"));
	    else {
		fd = popen("uname -n", "r");
		if (fd && fgets(buf, sizeof(buf), fd)) {
			p = strchr(buf, '\n'); if (p) *p = '\0';
			pclose(fd);
		}
		else strcpy(buf, "localhost");
	    }
		evar = (char *)malloc(strlen(buf) + 13);
		sprintf(evar, "MACHINEDOTS=%s", buf);
		putenv(evar);
	}

	xgetenv("MACHINE");

	if (getenv("SERVEROSTYPE") == NULL) {
		fd = popen("uname -s", "r");
		if (fd && fgets(buf, sizeof(buf), fd)) {
			p = strchr(buf, '\n'); if (p) *p = '\0';
			pclose(fd);
		}
		else strcpy(buf, "unix");
		for (p=buf; (*p); p++) *p = (char) tolower((int)*p);

		evar = (char *)malloc(strlen(buf) + 14);
		sprintf(evar, "SERVEROSTYPE=%s", buf);
		putenv(evar);
	}

	if (getenv("XYMONCLIENTHOME") == NULL) {
		homedir = strdup(envfn);
		p = strrchr(homedir, '/');
		if (p) {
			*p = '\0';
			if (strlen(homedir) > 4) {
				p = homedir + strlen(homedir) - 4;
				if (strcmp(p, "/etc") == 0) {
					*p = '\0';
					evar = (char *)malloc(20 + strlen(homedir));
					sprintf(evar, "XYMONCLIENTHOME=%s", homedir);
					putenv(evar);
				}
			}
		}
	}
}

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
			fprintf(stdout, "Xymon version %s\n", VERSION);
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

		sprintf(envfn, "%s/etc/xymonserver.cfg", xgetenv("XYMONHOME"));
		if (stat(envfn, &st) == -1) sprintf(envfn, "%s/etc/xymonclient.cfg", xgetenv("XYMONHOME"));
		errprintf("Using default environment file %s\n", envfn);

		/* Make sure SERVEROSTYPE, MACHINEDOTS and MACHINE are setup for our child */
		xymon_default_envs(envfn);
		loadenv(envfn, envarea);
	}
	else {
		/* Make sure SERVEROSTYPE, MACHINEDOTS and MACHINE are setup for our child */
		xymon_default_envs(envfile);
		loadenv(envfile, envarea);
	}


	/* Go! */
	if (cmd == NULL) cmd = cmdargs[0] = "/bin/sh";
	execvp(cmd, cmdargs);

	/* Should never go here */
	errprintf("execvp() failed: %s\n", strerror(errno));

	return 0;
}

