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

#ifndef __LOADHOSTS_H_
#define __LOADHOSTS_H_

/* List definition to search for page records */
typedef struct pagelist_t {
	struct bbgen_page_t *pageentry;
	struct pagelist_t *next;
} pagelist_t;

extern int hostcount;
extern int pagecount;

extern link_t *load_all_links(void);
extern bbgen_page_t *load_bbhosts(char *pgset);

/* Needed by the summary handling */
extern host_t *init_host(const char *hostname, const char *displayname, const char *clientalias,
			 const char *comment, const char *description,
			 const int ip1, const int ip2, const int ip3, const int ip4,
			 const int dialup, const int prefer, 
			 const double warnpct, const char *reporttime,
			 char *alerts, int nktime, char *waps, char *tags, 
			 char *nopropyellowtests, char *nopropredtests, char *noproppurpletests, char *nopropacktests,
			 char *larrdgraphs, int modembanksize);

extern char	*nopropyellowdefault;
extern char	*nopropreddefault;
extern char	*noproppurpledefault;
extern char	*nopropackdefault;
extern char	*larrdgraphs_default;
extern char     *wapcolumns;
extern time_t   snapshot;

#endif
