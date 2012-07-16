/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MULTICOLUMN_H_
#define __MULTICOLUMN_H_

extern void clear_multicolumn_message(void);
extern void add_multicolumn_message(char *columnname, int color, char *txt, char *summarytxt);
extern void flush_multicolumn_message(char *hostname, char *line1txt, char *fromline, char *alertgroups);

#endif

