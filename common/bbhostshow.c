/*----------------------------------------------------------------------------*/
/* Big Brother bb-hosts file viewer                                           */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbhostshow.c,v 1.6 2005-01-18 22:25:59 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "version.h"
#include "libbbgen.h"

int main(int argc, char *argv[])
{ 
	FILE *bbhosts;
	char fn[PATH_MAX];
	char l[MAX_LINE_LEN];
	int argi;
	char *include2 = NULL;


	fn[0] = '\0';

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--version") == 0) {
			printf("bbhostshow version %s\n", VERSION);
			exit(0);
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			printf("Usage:\n%s [filename]\n", argv[0]);
			exit(0);
		}
		else if (strcmp(argv[argi], "--bbnet") == 0) {
			include2 = "netinclude";
		}
		else if (strcmp(argv[argi], "--bbdisp") == 0) {
			include2 = "dispinclude";
		}
		else if (*argv[argi] != '-') {
			strcpy(fn, argv[argi]);
		}
	}

	if (strlen(fn) == 0) {
		if (xgetenv("BBHOSTS")) {
			strcpy(fn, xgetenv("BBHOSTS"));
		}
		else {
			errprintf("Environment variable BBHOSTS is not set - aborting\n");
			exit(2);
		}
	}

	bbhosts = stackfopen(fn, "r");
	if (bbhosts == NULL) {
		printf("Cannot open the BBHOSTS file '%s'\n", argv[1]);
		exit(1);
	}

	while (stackfgets(l, sizeof(l), "include", include2)) {
		printf("%s", l);
	}

	stackfclose(bbhosts);
	return 0;
}

