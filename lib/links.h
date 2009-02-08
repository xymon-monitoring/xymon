/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LINKS_H__
#define __LINKS_H__

extern char *link_docext(char *fn);
extern void load_all_links(void);
extern char *columnlink(char *colname);
extern char *hostlink(char *hostname);
extern char *hostlink_filename(char *hostname);

#endif
