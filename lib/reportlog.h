/*----------------------------------------------------------------------------*/
/* Hobbit report-mode statuslog viewer.                                       */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BB_REPLOG_H__
#define __BB_REPLOG_H__

#define STYLE_CRIT 0
#define STYLE_NONGR 1
#define STYLE_OTHER 2

extern char *stylenames[];

extern void generate_replog(FILE *htmlrep, FILE *textrep, char *textrepurl,
		     char *hostname, char *ip, char *service, int color, int style,
		     time_t st, time_t end, double reportwarnlevel, double reportgreenlevel, 
		     reportinfo_t *repinfo);
#endif

