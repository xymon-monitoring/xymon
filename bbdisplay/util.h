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
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __UTIL_H_
#define __UTIL_H_

extern char *htmlextension;

extern hostlist_t *hosthead;
extern link_t *linkhead;
extern link_t null_link;

extern char *alttag(entry_t *e);
extern char *hostpage_link(host_t *host);
extern char *hostpage_name(host_t *host);
extern int checkalert(char *alertlist, char *test);
extern int checkpropagation(host_t *host, char *test, int color, int acked);
extern link_t *find_link(const char *name);
extern char *columnlink(link_t *link, char *colname);
extern char *hostlink(link_t *link);
extern char *urldoclink(const char *docurl, const char *hostname);
extern host_t *find_host(const char *hostname);
extern bbgen_col_t *find_or_create_column(const char *testname, int create);
extern char *histlogurl(char *hostname, char *service, time_t histtime);
extern int run_columngen(char *column, int update_interval, int enabled);
extern void drop_genstatfiles(void);

#endif

