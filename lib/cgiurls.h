/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CGIURLS_H__
#define __CGIURLS_H__

#include <time.h>
#include "availability.h"

extern char *hostsvcurl(char *hostname, char *service, char *ip, char *displayname);
extern char *histcgiurl(char *hostname, char *service);
extern char *histlogurl(char *hostname, char *service, char *displayname, time_t histtime, char *histtime_txt);
extern char *replogurl(char *hostname, char *service, char *ip, char *displayname, int color, 
			char *style, int recentgifs,
			reportinfo_t *repinfo, 
			char *reportstart, time_t reportend, float reportwarnlevel);

#endif

