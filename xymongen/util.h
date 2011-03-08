/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __UTIL_H_
#define __UTIL_H_

extern char *htmlextension;

extern char *hostpage_link(host_t *host);
extern char *hostpage_name(host_t *host);
extern int checkpropagation(host_t *host, char *test, int color, int acked);
extern host_t *find_host(char *hostname);
extern int host_exists(char *hostname);
extern hostlist_t *find_hostlist(char *hostname);
extern hostlist_t *hostlistBegin(void);
extern hostlist_t *hostlistNext(void);
extern void add_to_hostlist(hostlist_t *rec);
extern bbgen_col_t *find_or_create_column(char *testname, int create);

#endif

