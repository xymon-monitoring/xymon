/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for generating the Xymon CGI URL's                    */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "libxymon.h"

static char *cgibinurl = NULL;

char *hostsvcurl(char *hostname, char *service, int htmlformat)
{
	static char *url;
	unsigned int url_buflen;

	if (url) xfree(url);
	if (!cgibinurl) cgibinurl = xgetenv("CGIBINURL");

	url_buflen = 1024 + strlen(cgibinurl) + strlen(hostname) + strlen(service);
	url = (char *)malloc(url_buflen);
	
	snprintf(url, url_buflen, 
		(htmlformat ? "%s/svcstatus.sh?HOST=%s&amp;SERVICE=%s" : "%s/svcstatus.sh?HOST=%s&SERVICE=%s"), 
		cgibinurl, hostname, service);

	return url;
}

char *hostsvcclienturl(char *hostname, char *section)
{
	static char *url;
	unsigned int url_buflen;
	int n;

	if (url) xfree(url);
	if (!cgibinurl) cgibinurl = xgetenv("CGIBINURL");

	url_buflen = 1024 + strlen(cgibinurl) + strlen(hostname) + (section ? strlen(section) : 0);
	url = (char *)malloc(url_buflen);
	n = snprintf(url, url_buflen, "%s/svcstatus.sh?CLIENT=%s", cgibinurl, hostname);

	if (section) snprintf(url+n, (url_buflen - n), "&amp;SECTION=%s", section);

	return url;
}

char *histcgiurl(char *hostname, char *service)
{
	static char *url = NULL;
	unsigned int url_buflen;

	if (url) xfree(url);
	if (!cgibinurl) cgibinurl = xgetenv("CGIBINURL");

	url_buflen = 1024 + strlen(cgibinurl) + strlen(hostname) + strlen(service);
	url = (char *)malloc(url_buflen);
	snprintf(url, url_buflen, "%s/history.sh?HISTFILE=%s.%s", cgibinurl, commafy(hostname), service);

	return url;
}

char *histlogurl(char *hostname, char *service, time_t histtime, char *histtime_txt)
{
	static char *url = NULL;
	unsigned int url_buflen;

	if (url) xfree(url);
	if (!cgibinurl) cgibinurl = xgetenv("CGIBINURL");

	/* cgi-bin/historylog.sh?HOST=foo.sample.com&SERVICE=msgs&TIMEBUF=Fri_Nov_7_16:01:08_2002 */
	url_buflen = 1024 + strlen(cgibinurl) + strlen(hostname) + strlen(service);
	url = (char *)malloc(url_buflen);
	if (!histtime_txt) {
		snprintf(url, url_buflen, "%s/historylog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s",
			xgetenv("CGIBINURL"), hostname, service, histlogtime(histtime));
	}
	else {
		snprintf(url, url_buflen, "%s/historylog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s",
			xgetenv("CGIBINURL"), hostname, service, histtime_txt);
	}

	return url;
}

char *replogurl(char *hostname, char *service, int color, 
		char *style, int recentgifs,
		reportinfo_t *repinfo, 
		char *reportstart, time_t reportend, float reportwarnlevel)
{
	static char *url;
	unsigned int url_buflen, n;

	if (url) xfree(url);
	if (!cgibinurl) cgibinurl = xgetenv("CGIBINURL");

	url_buflen = 4096 + strlen(cgibinurl) + strlen(hostname) + strlen(service);
	url = (char *)malloc(url_buflen);
	n = snprintf(url, url_buflen, "%s/reportlog.sh?HOST=%s&amp;SERVICE=%s&amp;COLOR=%s&amp;PCT=%.2f&amp;ST=%u&amp;END=%u&amp;RED=%.2f&amp;YEL=%.2f&amp;GRE=%.2f&amp;PUR=%.2f&amp;CLE=%.2f&amp;BLU=%.2f&amp;STYLE=%s&amp;FSTATE=%s&amp;REDCNT=%d&amp;YELCNT=%d&amp;GRECNT=%d&amp;PURCNT=%d&amp;CLECNT=%d&amp;BLUCNT=%d&amp;WARNPCT=%.2f&amp;RECENTGIFS=%d",
		cgibinurl, 
		hostname, service,
		colorname(color), repinfo->fullavailability, 
		(unsigned int)repinfo->reportstart, (unsigned int)reportend,
		repinfo->fullpct[COL_RED], repinfo->fullpct[COL_YELLOW], 
		repinfo->fullpct[COL_GREEN], repinfo->fullpct[COL_PURPLE], 
		repinfo->fullpct[COL_CLEAR], repinfo->fullpct[COL_BLUE],
		style, repinfo->fstate,
		repinfo->count[COL_RED], repinfo->count[COL_YELLOW], 
		repinfo->count[COL_GREEN], repinfo->count[COL_PURPLE], 
		repinfo->count[COL_CLEAR], repinfo->count[COL_BLUE],
		reportwarnlevel,
		use_recentgifs);
	if (reportstart) snprintf(url+n, (url_buflen - n), "&amp;REPORTTIME=%s", reportstart);

	return url;
}

