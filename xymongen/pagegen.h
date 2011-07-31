/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
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
extern char *nongreenignorecolumns;
extern int  nongreencolors;
extern int sort_grouponly_items;
extern char *documentationurl;
extern char *doctargetspec;
extern char *rssextension;
extern char *defaultpagetitle;
extern char *eventignorecolumns;
extern char *htaccess;
extern char *xymonhtaccess;
extern char *xymonpagehtaccess;
extern char *xymonsubpagehtaccess;
extern int  pagetitlelinks;
extern int  pagetextheadings;
extern int  underlineheadings;
extern int  maxrowsbeforeheading;
extern int  nongreeneventlog;
extern int  nongreenacklog;
extern int  nongreeneventlogmaxcount;
extern int  nongreeneventlogmaxtime;
extern int  nongreennodialups;
extern int  nongreenacklogmaxcount;
extern int  nongreenacklogmaxtime;
extern char *logcritstatus;
extern int  critonlyreds;
extern int  wantrss;

extern void select_headers_and_footers(char *prefix);
extern void do_one_page(xymongen_page_t *page, dispsummary_t *sums, int embedded);
extern void do_page_with_subs(xymongen_page_t *curpage, dispsummary_t *sums);
extern int  do_nongreen_page(char *nssidebarfilename, int summarytype);

#endif
