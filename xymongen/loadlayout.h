/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADLAYOUT_H__
#define __LOADLAYOUT_H__

extern int hostcount;
extern int pagecount;

extern xymongen_page_t *load_layout(char *pgset);

/* Needed by the summary handling */
extern host_t *init_host(char *hostname, int issummary,
			 char *displayname, char *clientalias,
			 char *comment, char *description,
			 int ip1, int ip2, int ip3, int ip4,
			 int dialup,
			 double warnpct, int warnstops, char *reporttime,
			 char *alerts, int crittime, char *waps,
			 char *nopropyellowtests, char *nopropredtests, char *noproppurpletests, char *nopropacktests);

extern char	*nopropyellowdefault;
extern char	*nopropreddefault;
extern char	*noproppurpledefault;
extern char	*nopropackdefault;
extern char     *wapcolumns;
extern time_t   snapshot;

#endif

