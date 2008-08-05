/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADHOSTS_H__
#define __LOADHOSTS_H__

enum bbh_item_t { 
	BBH_NET,
	BBH_DISPLAYNAME, 
	BBH_CLIENTALIAS, 
	BBH_COMMENT,
	BBH_DESCRIPTION,
	BBH_NK,
	BBH_NKTIME,
	BBH_TRENDS,
	BBH_WML,
	BBH_NOPROPRED,
	BBH_NOPROPYELLOW,
	BBH_NOPROPPURPLE,
	BBH_NOPROPACK,
	BBH_REPORTTIME,
	BBH_WARNPCT,
	BBH_WARNSTOPS,
	BBH_DOWNTIME,
	BBH_SSLDAYS,
	BBH_SSLMINBITS,
	BBH_DEPENDS,
	BBH_BROWSER,
	BBH_HOLIDAYS,
	BBH_FLAG_NOINFO,
	BBH_FLAG_NOTRENDS,
	BBH_FLAG_NODISP,
	BBH_FLAG_NOBB2,
	BBH_FLAG_PREFER,
	BBH_FLAG_NOSSLCERT,
	BBH_FLAG_TRACE,
	BBH_FLAG_NOTRACE,
	BBH_FLAG_NOCONN,
	BBH_FLAG_NOPING,
	BBH_FLAG_DIALUP,
	BBH_FLAG_TESTIP,
	BBH_FLAG_BBDISPLAY,
	BBH_FLAG_BBNET,
	BBH_FLAG_BBPAGER,
	BBH_FLAG_LDAPFAILYELLOW,
	BBH_FLAG_NOCLEAR,
	BBH_FLAG_HIDEHTTP,
	BBH_FLAG_PULLDATA,
	BBH_LDAPLOGIN,
	BBH_IP,
	BBH_HOSTNAME,
	BBH_DOCURL,
	BBH_NOPROP,
	BBH_PAGEINDEX,
	BBH_GROUPID,
	BBH_PAGENAME,
	BBH_PAGEPATH,
	BBH_PAGETITLE,
	BBH_PAGEPATHTITLE,
	BBH_ALLPAGEPATHS,
	BBH_RAW,
	BBH_CLASS,
	BBH_OS,
	BBH_NOCOLUMNS,
	BBH_DATA,
	BBH_NOTBEFORE,
	BBH_NOTAFTER,
	BBH_LAST
};

extern void load_hostnames(char *bbhostsfn, char *extrainclude, int fqdn);
extern char *knownhost(char *filename, char *hostip, int ghosthandling);
extern int knownloghost(char *logdir);
extern void *hostinfo(char *hostname);
extern void *localhostinfo(char *hostname);
extern char *bbh_item(void *host, enum bbh_item_t item);
extern char *bbh_custom_item(void *host, char *key);
extern enum bbh_item_t bbh_key_idx(char *item);
extern char *bbh_item_byname(void *host, char *item);
extern char *bbh_item_walk(void *host);
extern int bbh_item_idx(char *value);
extern char *bbh_item_id(enum bbh_item_t idx);
extern void *first_host(void);
extern void *next_host(void *currenthost, int wantclones);
extern void bbh_set_item(void *host, enum bbh_item_t item, void *value);
extern char *bbh_item_multi(void *host, enum bbh_item_t item);

#endif

