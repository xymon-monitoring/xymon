/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
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
	BBH_DOWNTIME,
	BBH_SSLDAYS,
	BBH_DEPENDS,
	BBH_BROWSER,
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
	BBH_BANKSIZE,
	BBH_DOCURL,
	BBH_NOPROP,
	BBH_PAGEINDEX,
	BBH_GROUPID,
	BBH_PAGENAME,
	BBH_PAGEPATH,
	BBH_PAGETITLE,
	BBH_PAGEPATHTITLE,
	BBH_RAW,
	BBH_CLASS,
	BBH_OS,
	BBH_NOCOLUMNS,
	BBH_LAST
};

typedef struct pagelist_t {
	char *pagepath;
	char *pagetitle;
	struct pagelist_t *next;
} pagelist_t;

typedef struct namelist_t {
	char ip[IP_ADDR_STRLEN];
	char *bbhostname;	/* Name for item 2 of bb-hosts */
	char *logname;		/* Name of the host directory in BBHISTLOGS (underscores replaces dots). */
	int preference;		/* For host with multiple entries, mark if we have the preferred one */
	int banksize;		/* For modem-bank entries only */
	pagelist_t *page;	/* Host location in the page/subpage/subparent tree */
	void *data;		/* Misc. data supplied by the user of this library function */
	struct namelist_t *defaulthost;	/* Points to the latest ".default." host */
	int pageindex;
	char *groupid;
	char *classname;
	char *osname;
	struct namelist_t *next;

	char *rawentry;		/* The raw bb-hosts entry for this host. */
	char *allelems;		/* Storage for data pointed to by elems */
	char **elems;		/* List of pointers to the elements of the entry */

	/* 
	 * The following are pre-parsed elements from the "rawentry".
	 * These are pre-parsed because they are used by the hobbit daemon, so
	 * fast access to them is an optimization.
	 */
	char *clientname;	/* CLIENT: tag - host alias */
	char *downtime;		/* DOWNTIME tag - when host has planned downtime. */
} namelist_t;

extern namelist_t *load_hostnames(char *bbhostsfn, char *extrainclude, int fqdn);
extern char *knownhost(char *filename, char *hostip, int ghosthandling);
extern int knownloghost(char *logdir);
extern namelist_t *hostinfo(char *hostname);
extern namelist_t *localhostinfo(char *hostname);
extern char *bbh_item(namelist_t *host, enum bbh_item_t item);
extern char *bbh_custom_item(namelist_t *host, char *key);
extern enum bbh_item_t bbh_key_idx(char *item);
extern char *bbh_item_byname(namelist_t *host, char *item);
extern char *bbh_item_walk(namelist_t *host);
extern int bbh_item_idx(char *value);
extern char *bbh_item_id(enum bbh_item_t idx);
extern namelist_t *first_host(void);
extern void bbh_set_item(namelist_t *host, enum bbh_item_t item, char *value);

#endif

