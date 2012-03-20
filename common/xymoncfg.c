/*----------------------------------------------------------------------------*/
/* Xymon config file viewer                                                   */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "version.h"
#include "libxymon.h"

int main(int argc, char *argv[])
{ 
	FILE *cfgfile;
	char *fn = NULL;
	strbuffer_t *inbuf;
	int argi;
	char *include2 = NULL;


	for (argi=1; (argi < argc); argi++) {
		if ((strcmp(argv[argi], "--net") == 0) || (strcmp(argv[argi], "--bbnet") == 0)) {
			include2 = "netinclude";
		}
		else if ((strcmp(argv[argi], "--web") == 0) || (strcmp(argv[argi], "--bbdisp") == 0)) {
			include2 = "dispinclude";
		}
		else if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
		else if (*argv[argi] != '-') {
			fn = strdup(argv[argi]);
		}
	}

	if (!fn || (strlen(fn) == 0)) {
		fn = getenv("HOSTSCFG");
		if (!fn) {
			errprintf("Environment variable HOSTSCFG is not set - aborting\n");
			exit(2);
		}
	}

	cfgfile = stackfopen(fn, "r", NULL);
	if (cfgfile == NULL) {
		printf("Cannot open file '%s'\n", fn);
		exit(1);
	}

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, include2)) {
		printf("%s", STRBUF(inbuf));
	}

	stackfclose(cfgfile);
	freestrbuffer(inbuf);
	return 0;
}

