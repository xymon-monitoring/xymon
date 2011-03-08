/*----------------------------------------------------------------------------*/
/* Hobbit bb-hosts file viewer                                                */
/*                                                                            */
/* Copyright (C) 2003-2008 Henrik Storner <henrik@hswn.dk>                    */
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
#include "libbbgen.h"

int main(int argc, char *argv[])
{ 
	FILE *bbhosts;
	char *fn = NULL;
	strbuffer_t *inbuf;
	int argi;
	char *include2 = NULL;


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
			fn = strdup(argv[argi]);
		}
	}

	if (!fn || (strlen(fn) == 0)) {
		fn = getenv("BBHOSTS");
		if (!fn) {
			errprintf("Environment variable BBHOSTS is not set - aborting\n");
			exit(2);
		}
	}

	bbhosts = stackfopen(fn, "r", NULL);
	if (bbhosts == NULL) {
		printf("Cannot open the BBHOSTS file '%s'\n", fn);
		exit(1);
	}

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, include2)) {
		printf("%s", STRBUF(inbuf));
	}

	stackfclose(bbhosts);
	freestrbuffer(inbuf);
	return 0;
}

