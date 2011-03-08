/*----------------------------------------------------------------------------*/
/* Hobbit webpage generator tool.                                             */
/*                                                                            */
/* Copyright (C) 2003-2008 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __RSSGEN_H__
#define __RSSGEN_H__

extern char *rssversion;
extern int  rsscolorlimit;
extern int  nssidebarcolorlimit;

extern void do_rss_header(FILE *fd);
extern void do_rss_item(FILE *fd, host_t *h, entry_t *e);
extern void do_rss_footer(FILE *fd);

extern void do_netscape_sidebar(char *rssfilename, host_t *hosts);
#endif

