/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __REPORTLOG_H__
#define __REPORTLOG_H__

#include <stdio.h>
#include "xymon_availability.h"

#define STYLE_CRIT 0
#define STYLE_NONGR 1
#define STYLE_OTHER 2

extern char *stylenames[];

extern void generate_replog(FILE *htmlrep, FILE *textrep, char *textrepurl,
		     char *hostname, char *service, int color, int style,
		     char *ip, char *displayname,
		     time_t st, time_t end, double reportwarnlevel, double reportgreenlevel, int reportwarnstops,
		     reportinfo_t *repinfo);
#endif

