/*----------------------------------------------------------------------------*/
/* Big Brother bb-hosts file viewer                                           */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bbgen.h"
#include "util.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

int main(int argc, char *argv[])
{ 
	FILE *bbhosts;
	char fn[MAX_PATH];
	char l[MAX_LINE_LEN];

	if ((argc < 2) && getenv("BBHOSTS")) strcpy(fn, getenv("BBHOSTS"));
	else if (argc == 2) strcpy(fn, argv[1]);
	else {
		printf("Usage: bbhostshow [filename]\n");
		exit(1);
	}

	bbhosts = stackfopen(fn, "r");
	if (bbhosts == NULL) {
		printf("Cannot open the BBHOSTS file '%s'\n", argv[1]);
		exit(1);
	}

	while (stackfgets(l, sizeof(l), "include")) {
		printf("%s", l);
	}

	stackfclose(bbhosts);
	return 0;
}

