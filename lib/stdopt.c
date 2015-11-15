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

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

char *programname = NULL;
char *pidfn = NULL;
char *hostsfn = NULL;
char *logfn = NULL;
char *envarea = NULL;
int  showhelp = 0;
int  dontsendmessages = 0;


static void xymon_default_envs(char *envfn)
{
	FILE *fd;
	char buf[1024];
	char *evar;
	char *homedir, *p;
	int hasuname = 0;
#ifdef	HAVE_UNAME
	struct utsname u_name;
#endif

#ifdef	HAVE_UNAME
	hasuname = (uname(&u_name) != -1);
	if (!hasuname) errprintf("uname() failed: %s\n", strerror(errno));
#endif

	if (getenv("MACHINEDOTS") == NULL) {
	    if (getenv("HOSTNAME") != NULL) sprintf(buf, "%s", xgetenv("HOSTNAME"));
#ifdef	HAVE_UNAME
	    else if (hasuname) sprintf(buf, "%s", u_name.nodename);
#endif
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
#ifdef	HAVE_UNAME
	    if (hasuname) sprintf(buf, "%s", u_name.sysname);
	    else {
#else
	    {
#endif
		fd = popen("uname -s", "r");
		if (fd && fgets(buf, sizeof(buf), fd)) {
			p = strchr(buf, '\n'); if (p) *p = '\0';
			pclose(fd);
		}
		else strcpy(buf, "unix");
	    }
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


int standardoption(char *opt)
{
	int haveenv = 0;

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
		haveenv = 1;
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
	else if ((strcmp(opt, "--help") == 0) || (strcmp(opt, "-?") == 0)) {
		fprintf(stdout, "%s %s\n", programname, VERSION);
		showhelp = 1;
	}
	else if (strcmp(opt, "--version") == 0) {
		fprintf(stdout, "%s %s\n", programname, VERSION);
		exit(0);
	}
	else {
		return 0;
	}

	if (!haveenv) {
		struct stat st;
		char envfn[PATH_MAX];

		snprintf(envfn, sizeof(envfn), "%s/etc/xymonserver.cfg", xgetenv("XYMONHOME"));
		if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "/etc/xymon/xymonserver.cfg");
		if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "%s/etc/xymonclient.cfg", xgetenv("XYMONHOME"));
		if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "%s/etc/xymonclient.cfg", xgetenv("XYMONCLIENTHOME"));
		if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "/etc/xymon-client/xymonclient.cfg");
		if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "xymonserver.cfg");
		if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "xymonclient.cfg");

		dbgprintf("Using default environment file %s\n", envfn);
		loadenv(envfn, envarea);

		/* Make sure SERVEROSTYPE, MACHINEDOTS and MACHINE are setup for our child */
		xymon_default_envs(envfn);
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

