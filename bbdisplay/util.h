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

#ifndef __UTIL_H_
#define __UTIL_H_

extern char *colorname(int color);
extern int eventcolor(char *colortext);
extern char *dotgiffilename(entry_t *e);
extern char *alttag(entry_t *e);
extern char *commafy(char *hostname);
extern void headfoot(FILE *output, char *pagetype, char *pagename, char *subpagename, char *head_or_foot, int bgcolor);
extern int checkalert(host_t *host, char *test);

extern link_t *find_link(const char *name);
extern char *columnlink(link_t *link, char *colname);
extern char *hostlink(link_t *link);

extern host_t *find_host(const char *hostname);
extern char *histlogurl(char *hostname, char *service, time_t histtime);

#endif

