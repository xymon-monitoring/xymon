/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __PAGEGEN_H_
#define __PAGEGEN_H_

extern int subpagecolumns;
extern int hostsbeforepages;
extern char *includecolumns;
extern char *bb2ignorecolumns;
extern int  bb2colors;
extern int sort_grouponly_items;
extern char *documentationurl;
extern char *doctargetspec;
extern char *rssextension;
extern char *defaultpagetitle;
extern char *eventignorecolumns;
extern char *htaccess;
extern char *bbhtaccess;
extern char *bbpagehtaccess;
extern char *bbsubpagehtaccess;
extern int  pagetitlelinks;
extern int  pagetextheadings;
extern int  underlineheadings;
extern int  maxrowsbeforeheading;
extern int  bb2eventlog;
extern int  bb2acklog;
extern int  bb2eventlogmaxcount;
extern int  bb2eventlogmaxtime;
extern int  bb2nodialups;
extern int  bb2acklogmaxcount;
extern int  bb2acklogmaxtime;
extern char *lognkstatus;
extern int  nkonlyreds;
extern char *nkackname;
extern int  wantrss;

extern void select_headers_and_footers(char *prefix);
extern void do_one_page(bbgen_page_t *page, dispsummary_t *sums, int embedded);
extern void do_page_with_subs(bbgen_page_t *curpage, dispsummary_t *sums);
extern int  do_bb2_page(char *nssidebarfilename, int summarytype);

#endif
