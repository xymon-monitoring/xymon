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

#ifndef __LOADDATA_H_
#define __LOADDATA_H_

/* List definition to search for page records */
typedef struct {
	bbgen_page_t *pageentry;
	void *next;
} pagelist_t;

extern bbgen_col_t *find_or_create_column(const char *testname);
extern link_t *load_all_links(void);
extern bbgen_page_t *load_bbhosts(char *pgset);
extern state_t *load_state(dispsummary_t **sumhead);

/* Needed by the summary handling */
extern host_t *init_host(const char *hostname, const int ip1, const int ip2, const int ip3, const int ip4,
			 const int dialup, const char *alerts,
			 char *tags, const char *nopropyellowtests, const char *nopropredtests,
			 const char *larrdgraphs);

extern char	*nopropyellowdefault;
extern char	*nopropreddefault;
extern char	*larrdgraphs_default;
extern int	enable_purpleupd;
extern int	purpledelay;
extern char     *ignorecolumns;

#endif
