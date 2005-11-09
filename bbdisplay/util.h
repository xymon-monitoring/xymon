/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __UTIL_H_
#define __UTIL_H_

extern char *htmlextension;
extern hostlist_t *hosthead;

extern char *hostpage_link(host_t *host);
extern char *hostpage_name(host_t *host);
extern int checkpropagation(host_t *host, char *test, int color, int acked);
extern host_t *find_host(const char *hostname);
extern bbgen_col_t *find_or_create_column(const char *testname, int create);
extern char *histlogurl(char *hostname, char *service, time_t histtime);

#endif

