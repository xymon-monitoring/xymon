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

extern char *rssfilename;
extern char *rssversion;
extern int  rsscolorlimit;
extern char *nssidebarfilename;
extern int  nssidebarcolorlimit;

extern void do_rss_feed(void);
extern void do_netscape_sidebar(void);
#endif

