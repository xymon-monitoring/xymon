/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This file contains code to generate RSS/RDF format output of alerts.       */
/* It is heavily influenced by Jeff Stoner's bb_content-feed script.          */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@storner.dk>                      */
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

