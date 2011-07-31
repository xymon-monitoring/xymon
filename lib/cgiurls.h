/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CGIURLS_H__
#define __CGIURLS_H__

#include <time.h>
#include "availability.h"

extern char *hostsvcurl(char *hostname, char *service, int htmlformat);
extern char *hostsvcclienturl(char *hostname, char *section);
extern char *histcgiurl(char *hostname, char *service);
extern char *histlogurl(char *hostname, char *service, time_t histtime, char *histtime_txt);
extern char *replogurl(char *hostname, char *service, int color, 
			char *style, int recentgifs,
			reportinfo_t *repinfo, 
			char *reportstart, time_t reportend, float reportwarnlevel);

#endif

