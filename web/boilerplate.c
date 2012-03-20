/*----------------------------------------------------------------------------*/
/* Xymon webpage generator tool.                                             */
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

static void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	cgidata_t *cgidata, *cwalk;

	cgidata = cgi_request();
	if (cgidata == NULL) errormsg(cgi_error());

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwakl->value points to the value (may be an empty string).
		 */

		cwalk = cwalk->next;
	}
}

int main(int argc, char *argv[])
{
	int argi;
	char *hffile = "boilerplate";
	int bgcolor = COL_BLUE;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--hffile=")) {
			char *p = strchr(argv[argi], '=');
			hffile = strdup(p+1);
		}
		else if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
	}

	parse_query();

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	headfoot(stdout, hffile, "", "header", bgcolor);

	headfoot(stdout, hffile, "", "footer", bgcolor);

	return 0;
}

