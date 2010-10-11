/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __COLOR_H__
#define __COLOR_H__

#define COL_GREEN	0
#define COL_CLEAR 	1
#define COL_BLUE  	2
#define COL_PURPLE 	3
#define COL_YELLOW	4
#define COL_RED		5
#define COL_COUNT       (COL_RED+1)
#define COL_CLIENT	99

extern int use_recentgifs;

extern char *colorname(int color);
extern int parse_color(char *colortext);
extern int eventcolor(char *colortext);
extern char *dotgiffilename(int color, int acked, int oldage);
extern int colorset(char *colspec, int excludeset);

#endif

