/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADBBHOSTS_H__
#define __LOADBBHOSTS_H__

extern int hostcount;
extern int pagecount;

extern bbgen_page_t *load_bbhosts(char *pgset);

/* Needed by the summary handling */
extern host_t *init_host(const char *hostname, const char *displayname, const char *clientalias,
			 const char *comment, const char *description,
			 const int ip1, const int ip2, const int ip3, const int ip4,
			 const int dialup,
			 const double warnpct, const char *reporttime,
			 char *alerts, int nktime, char *waps,
			 char *nopropyellowtests, char *nopropredtests, char *noproppurpletests, char *nopropacktests,
			 int modembanksize);

extern char	*nopropyellowdefault;
extern char	*nopropreddefault;
extern char	*noproppurpledefault;
extern char	*nopropackdefault;
extern char	*larrdgraphs_default;
extern char     *wapcolumns;
extern time_t   snapshot;

#endif

