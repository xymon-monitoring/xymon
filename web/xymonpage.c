/*----------------------------------------------------------------------------*/
/* Xymon webpage generator tool.                                              */
/*                                                                            */
/* This is a generic webpage generator, that allows scripts to output a       */
/* standard Xymon-like webpage without having to deal with headers and        */
/* footers.                                                                   */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libxymon.h"
#include "version.h"

char *reqenv[] = {
	"XYMONHOME",
	NULL
};

int main(int argc, char *argv[])
{
	int argi;
	char *hffile = "stdnormal";
	int bgcolor = COL_BLUE;
	char inbuf[8192];
	int n;

	libxymon_init(argv[0]);
	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--hffile=")) {
			char *p = strchr(argv[argi], '=');
			hffile = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--color=")) {
			char *p = strchr(argv[argi], '=');
			bgcolor = parse_color(p+1);
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
	}

	envcheck(reqenv);

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	
	headfoot(stdout, hffile, "", "header", bgcolor);
	do {
		n = fread(inbuf, 1, sizeof(inbuf), stdin);
		if (n > 0) fwrite(inbuf, 1, n, stdout);
	} while (n == sizeof(inbuf));
	headfoot(stdout, hffile, "", "footer", bgcolor);

	return 0;
}

