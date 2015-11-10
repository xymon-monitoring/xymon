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
	enum { S_NONE, S_KSH, S_CSH } shelltype = S_NONE;
	char *p;

	libxymon_init(argv[0]);
	for (argi=1; (argi < argc); argi++) {
		if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
		else if ((strcmp(argv[argi], "--net") == 0) || (strcmp(argv[argi], "--bbnet") == 0)) {
			include2 = "netinclude";
		}
		else if ((strcmp(argv[argi], "--web") == 0) || (strcmp(argv[argi], "--bbdisp") == 0)) {
			include2 = "dispinclude";
		}
		else if (strcmp(argv[argi], "-s") == 0) {
			shelltype = S_KSH;
		}
		else if (strcmp(argv[argi], "-c") == 0) {
			shelltype = S_CSH;
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
		switch (shelltype) {
		  case S_NONE:
			printf("%s", STRBUF(inbuf));
			break;
		  case S_KSH:
			sanitize_input(inbuf, 1, 0);
			p = STRBUF(inbuf) + strspn(STRBUF(inbuf), "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
			if (*p == '=') {
				char *val = p+1;

				*p = '\0';
				p = val + strcspn(val, "\r\n"); *p = '\0';
				printf("%s=%s;export %s\n", STRBUF(inbuf), val, STRBUF(inbuf));
			}
			break;
		  case S_CSH:
			sanitize_input(inbuf, 1, 0);
			p = STRBUF(inbuf) + strspn(STRBUF(inbuf), "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
			if (*p == '=') {
				char *val = p+1;

				*p = '\0';
				p = val + strcspn(val, "\r\n"); *p = '\0';
				printf("setenv %s %s\n", STRBUF(inbuf), val);
			}
			break;
		}
	}

	stackfclose(cfgfile);
	freestrbuffer(inbuf);
	return 0;
}

