/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for error- and debug-message display.                 */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: errormsg.c,v 1.2 2004-11-23 21:45:47 henrik Exp $";

#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "errormsg.h"

char *errbuf = NULL;
int save_errbuf = 1;
static unsigned int errbufsize = 0;

int debug = 0;


void errprintf(const char *fmt, ...)
{
	char timestr[30];
	char msg[4096];
	va_list args;

	time_t now = time(NULL);

	strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(stderr, "%s ", timestr);

	va_start(args, fmt);
#ifdef NO_VSNPRINTF
	vsprintf(msg, fmt, args);
#else
	vsnprintf(msg, sizeof(msg), fmt, args);
#endif
	va_end(args);

	fprintf(stderr, "%s", msg);
	fflush(stderr);

	if (save_errbuf) {
		if (errbuf == NULL) {
			errbufsize = 8192;
			errbuf = (char *) malloc(errbufsize);
			*errbuf = '\0';
		}
		else if ((strlen(errbuf) + strlen(msg)) > errbufsize) {
			errbufsize += 8192;
			errbuf = (char *) realloc(errbuf, errbufsize);
		}

		strcat(errbuf, msg);
	}
}


void dprintf(const char *fmt, ...)
{
	va_list args;

	if (debug) {
		char timestr[30];
		time_t now = time(NULL);

		strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S",
			 localtime(&now));
		printf("%s ", timestr);

		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
		fflush(stdout);
	}
}

void flush_errbuf(void)
{
	if (errbuf) free(errbuf);
	errbuf = NULL;
}

