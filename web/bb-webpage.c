/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a generic webpage generator, that allows scripts to output a       */
/* standard BB-like webpage without having to deal with headers and footers.  */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-webpage.c,v 1.2 2005-01-15 17:38:55 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"
#include "version.h"

char *reqenv[] = {
	"BBHOME",
	NULL
};

int main(int argc, char *argv[])
{
	int argi;
	char *hffile = "bb";
	int bgcolor = COL_BLUE;
	char inbuf[8192];
	int n;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--hffile=")) {
			char *p = strchr(argv[argi], '=');
			hffile = xstrdup(p+1);
		}
		else if (argnmatch(argv[argi], "--color=")) {
			char *p = strchr(argv[argi], '=');
			bgcolor = parse_color(p+1);
		}
	}

	envcheck(reqenv);

	fprintf(stdout, "Content-type: text/html\n\n");
	
	headfoot(stdout, hffile, "", "header", bgcolor);
	do {
		n = fread(inbuf, sizeof(inbuf), 1, stdin);
		if (n > 0) fwrite(inbuf, n, 1, stdout);
	} while (n == sizeof(inbuf));
	headfoot(stdout, hffile, "", "footer", bgcolor);

	return 0;
}

