/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU Public License (GPL), version 2.    */
/* See the file "COPYING" for details.                                        */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __PAGEGEN_H_
#define __PAGEGEN_H_

extern void do_bb_page(page_t *page, dispsummary_t *sums, char *filename);
extern void do_page(page_t *page, char *filename, char *upperpagename);
extern void do_subpage(page_t *page, char *filename, char *upperpagename);
extern void do_bb2_page(char *filename, int summarytype);

#endif
