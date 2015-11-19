/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADHOSTS_H__
#define __LOADHOSTS_H__

enum xmh_item_t { 
	XMH_NET,
	XMH_DISPLAYNAME, 
	XMH_CLIENTALIAS, 
	XMH_COMMENT,
	XMH_DESCRIPTION,
	XMH_NK,
	XMH_NKTIME,
	XMH_TRENDS,
	XMH_WML,
	XMH_NOPROPRED,
	XMH_NOPROPYELLOW,
	XMH_NOPROPPURPLE,
	XMH_NOPROPACK,
	XMH_REPORTTIME,
	XMH_WARNPCT,
	XMH_WARNSTOPS,
	XMH_DOWNTIME,
	XMH_SSLDAYS,
	XMH_SSLMINBITS,
	XMH_DEPENDS,
	XMH_BROWSER,
	XMH_HTTPHEADERS,
	XMH_HOLIDAYS,
	XMH_DELAYRED,
	XMH_DELAYYELLOW,
	XMH_FLAG_NOINFO,
	XMH_FLAG_NOTRENDS,
	XMH_FLAG_NOCLIENT,
	XMH_FLAG_NODISP,
	XMH_FLAG_NONONGREEN,
	XMH_FLAG_NOBB2,
	XMH_FLAG_PREFER,
	XMH_FLAG_NOSSLCERT,
	XMH_FLAG_TRACE,
	XMH_FLAG_NOTRACE,
	XMH_FLAG_NOCONN,
	XMH_FLAG_NOPING,
	XMH_FLAG_DIALUP,
	XMH_FLAG_TESTIP,
	XMH_FLAG_LDAPFAILYELLOW,
	XMH_FLAG_NOCLEAR,
	XMH_FLAG_HIDEHTTP,
	XMH_PULLDATA,
	XMH_NOFLAP,
	XMH_FLAG_MULTIHOMED,
	XMH_FLAG_SNI,
	XMH_FLAG_NOSNI,
	XMH_FLAG_HTTP_HEADER_MATCH,
	XMH_LDAPLOGIN,
	XMH_IP,
	XMH_HOSTNAME,
	XMH_DOCURL,
	XMH_NOPROP,
	XMH_PAGEINDEX,
	XMH_GROUPID,
	XMH_DGNAME,
	XMH_PAGENAME,
	XMH_PAGEPATH,
	XMH_PAGETITLE,
	XMH_PAGEPATHTITLE,
	XMH_ALLPAGEPATHS,
	XMH_ACCEPT_ONLY,
	XMH_RAW,
	XMH_CLASS,
	XMH_OS,
	XMH_NOCOLUMNS,
	XMH_DATA,
	XMH_NOTBEFORE,
	XMH_NOTAFTER,
	XMH_COMPACT,
	XMH_INTERFACES,
	XMH_LAST
};

enum ghosthandling_t { GH_ALLOW, GH_IGNORE, GH_LOG, GH_MATCH };

typedef struct pagelist_t {
	char *pagepath;
	char *pagetitle;
	struct pagelist_t *next;
} pagelist_t;

typedef struct namelist_t {
	char *ip;
	char *hostname;		/* Name for item 2 of hosts.cfg */
	char *logname;		/* Name of the host directory in XYMONHISTLOGS (underscores replaces dots). */
	int preference;		/* For host with multiple entries, mark if we have the preferred one */
	pagelist_t *page;	/* Host location in the page/subpage/subparent tree */
	void *data;		/* Misc. data supplied by the user of this library function */
	struct namelist_t *defaulthost; /* Points to the latest ".default." host */
	int pageindex;
	char *groupid, *dgname;
	char *classname;
	char *osname;
	struct namelist_t *next, *prev;

	char *allelems;		/* Storage for data pointed to by elems */
	char **elems;		/* List of pointers to the elements of the entry */

	/*
	 * The following are pre-parsed elements.
	 * These are pre-parsed because they are used by the xymon daemon, so
	 * fast access to them is an optimization.
	 */                                                                     
	char *clientname;	/* CLIENT: tag - host alias */
	char *downtime;		/* DOWNTIME tag - when host has planned downtime. */
	time_t notbefore, notafter; /* NOTBEFORE and NOTAFTER tags as time_t values */
} namelist_t;

extern void freenamelist(namelist_t *rec);
extern int load_hostnames(char *hostsfn, char *extrainclude, int fqdn);
extern int load_hostinfo(char *hostname);
extern void initialize_hostlist(void);
extern void destroy_hosttree(void);
extern char *hostscfg_content(void);
extern char *knownhost(char *hostname, char **hostip, enum ghosthandling_t ghosthandling);
extern int knownloghost(char *logdir);
extern void *hostinfo(char *hostname);
extern void *localhostinfo(char *hostname);
extern char *xmh_item(void *host, enum xmh_item_t item);
extern char *xmh_custom_item(void *host, char *key);
extern enum xmh_item_t xmh_key_idx(char *item);
extern char *xmh_item_byname(void *host, char *item);
extern char *xmh_item_walk(void *host);
extern int xmh_item_idx(char *value);
extern char *xmh_item_id(enum xmh_item_t idx);
extern void *first_host(void);
extern void *next_host(void *currenthost, int wantclones);
extern void xmh_set_item(void *host, enum xmh_item_t item, void *value);
extern char *xmh_item_multi(void *host, enum xmh_item_t item);

#endif

