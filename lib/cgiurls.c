/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for generating the Hobbit CGI URL's                   */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: cgiurls.c,v 1.1 2006-01-13 12:49:00 henrik Exp $";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "libbbgen.h"

char *histlogurl(char *hostname, char *service, time_t histtime)
{
	static char *url = NULL;
	static char *cgibinurl = NULL;

	if (url) xfree(url);
	if (!cgibinurl) cgibinurl = xgetenv("CGIBINURL");

	/* cgi-bin/bb-histlog.sh?HOST=SLS-P-CE1.slsdomain.sls.dk&SERVICE=msgs&TIMEBUF=Fri_Nov_7_16:01:08_2002 */
	url = (char *)malloc(1024 + strlen(cgibinurl) + strlen(hostname) + strlen(service));
	sprintf(url, "%s/bb-histlog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s",
	xgetenv("CGIBINURL"), hostname, service, histlogtime(histtime));

	return url;
}

