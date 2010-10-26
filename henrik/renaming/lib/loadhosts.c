/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the hosts.cfg  */
/* file and keeping track of what hosts are known, their aliases and planned  */
/* downtime settings etc.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/


static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#include "libxymon.h"

typedef struct pagelist_t {
	char *pagepath;
	char *pagetitle;
	struct pagelist_t *next;
} pagelist_t;

typedef struct namelist_t {
	char ip[IP_ADDR_STRLEN];
	char *bbhostname;	/* Name for item 2 of hosts.cfg */
	char *logname;		/* Name of the host directory in BBHISTLOGS (underscores replaces dots). */
	int preference;		/* For host with multiple entries, mark if we have the preferred one */
	pagelist_t *page;	/* Host location in the page/subpage/subparent tree */
	void *data;		/* Misc. data supplied by the user of this library function */
	struct namelist_t *defaulthost;	/* Points to the latest ".default." host */
	int pageindex;
	char *groupid;
	char *classname;
	char *osname;
	struct namelist_t *next;

	char *rawentry;		/* The raw hosts.cfg entry for this host. */
	char *allelems;		/* Storage for data pointed to by elems */
	char **elems;		/* List of pointers to the elements of the entry */

	/* 
	 * The following are pre-parsed elements from the "rawentry".
	 * These are pre-parsed because they are used by the hobbit daemon, so
	 * fast access to them is an optimization.
	 */
	char *clientname;	/* CLIENT: tag - host alias */
	char *downtime;		/* DOWNTIME tag - when host has planned downtime. */
	time_t notbefore, notafter; /* NOTBEFORE and NOTAFTER tags as time_t values */
} namelist_t;

static pagelist_t *pghead = NULL;
static namelist_t *namehead = NULL;
static namelist_t *defaulthost = NULL;
static const char *bbh_item_key[BBH_LAST];
static const char *bbh_item_name[BBH_LAST];
static int bbh_item_isflag[BBH_LAST];
static int configloaded = 0;
static RbtHandle rbhosts;
static RbtHandle rbclients;

static void bbh_item_list_setup(void)
{
	static int setupdone = 0;
	int i;
	enum bbh_item_t bi;

	if (setupdone) return;

	/* Doing it this way makes sure the index matches the value */
	setupdone = 1;
	memset(bbh_item_key, 0, sizeof(bbh_item_key));
	memset(bbh_item_name, 0, sizeof(bbh_item_key));
	memset(bbh_item_isflag, 0, sizeof(bbh_item_isflag));
	bbh_item_key[BBH_NET]                  = "NET:";
	bbh_item_name[BBH_NET]                 = "BBH_NET";
	bbh_item_key[BBH_DISPLAYNAME]          = "NAME:";
	bbh_item_name[BBH_DISPLAYNAME]         = "BBH_DISPLAYNAME";
	bbh_item_key[BBH_CLIENTALIAS]          = "CLIENT:";
	bbh_item_name[BBH_CLIENTALIAS]         = "BBH_CLIENTALIAS";
	bbh_item_key[BBH_COMMENT]              = "COMMENT:";
	bbh_item_name[BBH_COMMENT]             = "BBH_COMMENT";
	bbh_item_key[BBH_DESCRIPTION]          = "DESCR:";
	bbh_item_name[BBH_DESCRIPTION]         = "BBH_DESCRIPTION";
	bbh_item_key[BBH_DOCURL]               = "DOC:";
	bbh_item_name[BBH_DOCURL]              = "BBH_DOCURL";
	bbh_item_key[BBH_NK]                   = "NK:";
	bbh_item_name[BBH_NK]                  = "BBH_NK";
	bbh_item_key[BBH_NKTIME]               = "NKTIME=";
	bbh_item_name[BBH_NKTIME]              = "BBH_NKTIME";
	bbh_item_key[BBH_TRENDS]               = "TRENDS:";
	bbh_item_name[BBH_TRENDS]              = "BBH_TRENDS";
	bbh_item_key[BBH_WML]                  = "WML:";
	bbh_item_name[BBH_WML]                 = "BBH_WML";
	bbh_item_key[BBH_NOPROP]               = "NOPROP:";
	bbh_item_name[BBH_NOPROP]              = "BBH_NOPROP";
	bbh_item_key[BBH_NOPROPRED]            = "NOPROPRED:";
	bbh_item_name[BBH_NOPROPRED]           = "BBH_NOPROPRED";
	bbh_item_key[BBH_NOPROPYELLOW]         = "NOPROPYELLOW:";
	bbh_item_name[BBH_NOPROPYELLOW]        = "BBH_NOPROPYELLOW";
	bbh_item_key[BBH_NOPROPPURPLE]         = "NOPROPPURPLE:";
	bbh_item_name[BBH_NOPROPPURPLE]        = "BBH_NOPROPPURPLE";
	bbh_item_key[BBH_NOPROPACK]            = "NOPROPACK:";
	bbh_item_name[BBH_NOPROPACK]           = "BBH_NOPROPACK";
	bbh_item_key[BBH_REPORTTIME]           = "REPORTTIME=";
	bbh_item_name[BBH_REPORTTIME]          = "BBH_REPORTTIME";
	bbh_item_key[BBH_WARNPCT]              = "WARNPCT:";
	bbh_item_name[BBH_WARNPCT]             = "BBH_WARNPCT";
	bbh_item_key[BBH_WARNSTOPS]            = "WARNSTOPS:";
	bbh_item_name[BBH_WARNSTOPS]           = "BBH_WARNSTOPS";
	bbh_item_key[BBH_DOWNTIME]             = "DOWNTIME=";
	bbh_item_name[BBH_DOWNTIME]            = "BBH_DOWNTIME";
	bbh_item_key[BBH_SSLDAYS]              = "ssldays=";
	bbh_item_name[BBH_SSLDAYS]             = "BBH_SSLDAYS";
	bbh_item_key[BBH_SSLMINBITS]           = "sslbits=";
	bbh_item_name[BBH_SSLMINBITS]          = "BBH_SSLMINBITS";
	bbh_item_key[BBH_DEPENDS]              = "depends=";
	bbh_item_name[BBH_DEPENDS]             = "BBH_DEPENDS";
	bbh_item_key[BBH_BROWSER]              = "browser=";
	bbh_item_name[BBH_BROWSER]             = "BBH_BROWSER";
	bbh_item_key[BBH_HOLIDAYS]             = "holidays=";
	bbh_item_name[BBH_HOLIDAYS]            = "BBH_HOLIDAYS";
	bbh_item_key[BBH_FLAG_NOINFO]          = "noinfo";
	bbh_item_name[BBH_FLAG_NOINFO]         = "BBH_FLAG_NOINFO";
	bbh_item_key[BBH_FLAG_NOTRENDS]        = "notrends";
	bbh_item_name[BBH_FLAG_NOTRENDS]       = "BBH_FLAG_NOTRENDS";
	bbh_item_key[BBH_FLAG_NODISP]          = "nodisp";
	bbh_item_name[BBH_FLAG_NODISP]         = "BBH_FLAG_NODISP";
	bbh_item_key[BBH_FLAG_NOBB2]           = "nobb2";
	bbh_item_name[BBH_FLAG_NOBB2]          = "BBH_FLAG_NOBB2";
	bbh_item_key[BBH_FLAG_PREFER]          = "prefer";
	bbh_item_name[BBH_FLAG_PREFER]         = "BBH_FLAG_PREFER";
	bbh_item_key[BBH_FLAG_NOSSLCERT]       = "nosslcert";
	bbh_item_name[BBH_FLAG_NOSSLCERT]      = "BBH_FLAG_NOSSLCERT";
	bbh_item_key[BBH_FLAG_TRACE]           = "trace";
	bbh_item_name[BBH_FLAG_TRACE]          = "BBH_FLAG_TRACE";
	bbh_item_key[BBH_FLAG_NOTRACE]         = "notrace";
	bbh_item_name[BBH_FLAG_NOTRACE]        = "BBH_FLAG_NOTRACE";
	bbh_item_key[BBH_FLAG_NOCONN]          = "noconn";
	bbh_item_name[BBH_FLAG_NOCONN]         = "BBH_FLAG_NOCONN";
	bbh_item_key[BBH_FLAG_NOPING]          = "noping";
	bbh_item_name[BBH_FLAG_NOPING]         = "BBH_FLAG_NOPING";
	bbh_item_key[BBH_FLAG_DIALUP]          = "dialup";
	bbh_item_name[BBH_FLAG_DIALUP]         = "BBH_FLAG_DIALUP";
	bbh_item_key[BBH_FLAG_TESTIP]          = "testip";
	bbh_item_name[BBH_FLAG_TESTIP]         = "BBH_FLAG_TESTIP";
	bbh_item_key[BBH_FLAG_LDAPFAILYELLOW]  = "ldapyellowfail";
	bbh_item_name[BBH_FLAG_LDAPFAILYELLOW] = "BBH_FLAG_LDAPFAILYELLOW";
	bbh_item_key[BBH_FLAG_NOCLEAR]         = "NOCLEAR";
	bbh_item_name[BBH_FLAG_NOCLEAR]        = "BBH_FLAG_NOCLEAR";
	bbh_item_key[BBH_FLAG_HIDEHTTP]        = "HIDEHTTP";
	bbh_item_name[BBH_FLAG_HIDEHTTP]       = "BBH_FLAG_HIDEHTTP";
	bbh_item_key[BBH_FLAG_PULLDATA]        = "PULLDATA";
	bbh_item_name[BBH_FLAG_PULLDATA]       = "BBH_FLAG_PULLDATA";
	bbh_item_key[BBH_FLAG_MULTIHOMED]      = "MULTIHOMED";
	bbh_item_name[BBH_FLAG_MULTIHOMED]     = "BBH_MULTIHOMED";
	bbh_item_key[BBH_LDAPLOGIN]            = "ldaplogin=";
	bbh_item_name[BBH_LDAPLOGIN]           = "BBH_LDAPLOGIN";
	bbh_item_key[BBH_CLASS]                = "CLASS:";
	bbh_item_name[BBH_CLASS]               = "BBH_CLASS";
	bbh_item_key[BBH_OS]                   = "OS:";
	bbh_item_name[BBH_OS]                  = "BBH_OS";
	bbh_item_key[BBH_NOCOLUMNS]            = "NOCOLUMNS:";
	bbh_item_name[BBH_NOCOLUMNS]           = "BBH_NOCOLUMNS";
	bbh_item_key[BBH_NOTBEFORE]            = "NOTBEFORE:";
	bbh_item_name[BBH_NOTBEFORE]           = "BBH_NOTBEFORE";
	bbh_item_key[BBH_NOTAFTER]             = "NOTAFTER:";
	bbh_item_name[BBH_NOTAFTER]            = "BBH_NOTAFTER";
	bbh_item_key[BBH_COMPACT]              = "COMPACT:";
	bbh_item_name[BBH_COMPACT]             = "BBH_COMPACT";

	bbh_item_name[BBH_IP]                  = "BBH_IP";
	bbh_item_name[BBH_CLIENTALIAS]         = "BBH_CLIENTALIAS";
	bbh_item_name[BBH_HOSTNAME]            = "BBH_HOSTNAME";
	bbh_item_name[BBH_PAGENAME]            = "BBH_PAGENAME";
	bbh_item_name[BBH_PAGEPATH]            = "BBH_PAGEPATH";
	bbh_item_name[BBH_PAGETITLE]           = "BBH_PAGETITLE";
	bbh_item_name[BBH_PAGEPATHTITLE]       = "BBH_PAGEPATHTITLE";
	bbh_item_name[BBH_ALLPAGEPATHS]        = "BBH_ALLPAGEPATHS";
	bbh_item_name[BBH_GROUPID]             = "BBH_GROUPID";
	bbh_item_name[BBH_PAGEINDEX]           = "BBH_PAGEINDEX";
	bbh_item_name[BBH_RAW]                 = "BBH_RAW";

	i = 0; while (bbh_item_key[i]) i++;
	if (i != BBH_IP) {
		errprintf("ERROR: Setup failure in bbh_item_key position %d\n", i);
	}

	for (bi = 0; (bi < BBH_LAST); bi++) 
		if (bbh_item_name[bi]) bbh_item_isflag[bi] = (strncmp(bbh_item_name[bi], "BBH_FLAG_", 9) == 0);
}


static char *bbh_find_item(namelist_t *host, enum bbh_item_t item)
{
	int i;
	char *result;

	if (item == BBH_LAST) return NULL;	/* Unknown item requested */

	bbh_item_list_setup();
	i = 0;
	while (host->elems[i] && strncasecmp(host->elems[i], bbh_item_key[item], strlen(bbh_item_key[item]))) i++;
	result = (host->elems[i] ? (host->elems[i] + strlen(bbh_item_key[item])) : NULL);

	/* Handle the LARRD: tag in Xymon 4.0.4 and earlier */
	if (!result && (item == BBH_TRENDS)) {
		i = 0;
		while (host->elems[i] && strncasecmp(host->elems[i], "LARRD:", 6)) i++;
		result = (host->elems[i] ? (host->elems[i] + 6) : NULL);
	}

	if (result || !host->defaulthost || (strcasecmp(host->bbhostname, ".default.") == 0)) {
		if (bbh_item_isflag[item]) {
			return (result ? bbh_item_key[item] : NULL);
		}
		else
			return result;
	}
	else
		return bbh_find_item(host->defaulthost, item);
}

static void initialize_hostlist(void)
{
	while (defaulthost) {
		namelist_t *walk = defaulthost;
		defaulthost = defaulthost->defaulthost;

		if (walk->bbhostname) xfree(walk->bbhostname);
		if (walk->groupid) xfree(walk->groupid);
		if (walk->classname) xfree(walk->classname);
		if (walk->osname) xfree(walk->osname);
		if (walk->logname) xfree(walk->logname);
		if (walk->allelems) xfree(walk->allelems);
		if (walk->elems) xfree(walk->elems);
		xfree(walk);
	}

	while (namehead) {
		namelist_t *walk = namehead;

		namehead = namehead->next;

		/* clientname should not be freed, since it's just a pointer into the elems-array */
		if (walk->bbhostname) xfree(walk->bbhostname);
		if (walk->groupid) xfree(walk->groupid);
		if (walk->classname) xfree(walk->classname);
		if (walk->osname) xfree(walk->osname);
		if (walk->logname) xfree(walk->logname);
		if (walk->allelems) xfree(walk->allelems);
		if (walk->elems) xfree(walk->elems);
		xfree(walk);
	}

	while (pghead) {
		pagelist_t *walk = pghead;

		pghead = pghead->next;
		if (walk->pagepath) xfree(walk->pagepath);
		if (walk->pagetitle) xfree(walk->pagetitle);
		xfree(walk);
	}

	/* Setup the top-level page */
	pghead = (pagelist_t *) malloc(sizeof(pagelist_t));
	pghead->pagepath = strdup("");
	pghead->pagetitle = strdup("");
	pghead->next = NULL;
}

static void build_hosttree(void)
{
	static int hosttree_exists = 0;
	namelist_t *walk;
	RbtStatus status;
	char *tstr;

	if (hosttree_exists) {
		rbtDelete(rbhosts);
		rbtDelete(rbclients);
	}
	rbhosts = rbtNew(name_compare);
	rbclients = rbtNew(name_compare);
	hosttree_exists = 1;

	for (walk = namehead; (walk); walk = walk->next) {
		status = rbtInsert(rbhosts, walk->bbhostname, walk);
		if (walk->clientname) rbtInsert(rbclients, walk->clientname, walk);

		switch (status) {
		  case RBT_STATUS_OK:
		  case RBT_STATUS_DUPLICATE_KEY:
			break;
		  case RBT_STATUS_MEM_EXHAUSTED:
			errprintf("loadhosts:build_hosttree - insert into tree failed (out of memory)\n");
			break;
		  default:
			errprintf("loadhosts:build_hosttree - insert into tree failed code %d\n", status);
			break;
		}

		tstr = bbh_item(walk, BBH_NOTBEFORE);
		walk->notbefore = (tstr ? timestr2timet(tstr) : 0);
		if (walk->notbefore == -1) walk->notbefore = 0;

		tstr = bbh_item(walk, BBH_NOTAFTER);
		walk->notafter = (tstr ? timestr2timet(tstr) : INT_MAX);
		if (walk->notafter == -1) walk->notafter = INT_MAX;
	}
}

#include "loadhosts_file.c"

char *knownhost(char *hostname, char *hostip, enum ghosthandling_t ghosthandling)
{
	/*
	 * ghosthandling = GH_ALLOW  : Default BB method (case-sensitive, no logging, keep ghosts)
	 * ghosthandling = GH_IGNORE : Case-insensitive, no logging, drop ghosts
	 * ghosthandling = GH_LOG    : Case-insensitive, log ghosts, drop ghosts
	 * ghosthandling = GH_MATCH  : Like GH_LOG, but try to match unknown names against known hosts
	 */
	RbtIterator hosthandle;
	namelist_t *walk = NULL;
	static char *result = NULL;
	void *k1, *k2;
	time_t now = getcurrenttime(NULL);

	if (result) xfree(result);
	result = NULL;

	/* Find the host in the normal hostname list */
	hosthandle = rbtFind(rbhosts, hostname);
	if (hosthandle != rbtEnd(rbhosts)) {
		rbtKeyValue(rbhosts, hosthandle, &k1, &k2);
		walk = (namelist_t *)k2;
	}
	else {
		/* Not found - lookup in the client alias list */
		hosthandle = rbtFind(rbclients, hostname);
		if (hosthandle != rbtEnd(rbclients)) {
			rbtKeyValue(rbclients, hosthandle, &k1, &k2);
			walk = (namelist_t *)k2;
		}
	}

	if (walk) {
		/*
		 * Force our version of the hostname. Done here so CLIENT works always.
		 */
		strcpy(hostip, walk->ip);
		result = strdup(walk->bbhostname);
	}
	else {
		*hostip = '\0';
		result = strdup(hostname);
	}

	/* If default method, just say yes */
	if (ghosthandling == GH_ALLOW) return result;

	/* Allow all summaries */
	if (strcmp(hostname, "summary") == 0) return result;

	if (walk && ( ((walk->notbefore > now) || (walk->notafter < now)) )) walk = NULL;
	return (walk ? result : NULL);
}

int knownloghost(char *logdir)
{
	namelist_t *walk = NULL;

	/* Find the host */
	/* Must do the linear string search, since the tree is indexed by the bbhostname, not logname */
	for (walk = namehead; (walk && (strcasecmp(walk->logname, logdir) != 0)); walk = walk->next);

	return (walk != NULL);
}

void *hostinfo(char *hostname)
{
	RbtIterator hosthandle;
	namelist_t *result = NULL;
	time_t now = getcurrenttime(NULL);

	if (!configloaded) load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());

	hosthandle = rbtFind(rbhosts, hostname);
	if (hosthandle != rbtEnd(rbhosts)) {
		void *k1, *k2;

		rbtKeyValue(rbhosts, hosthandle, &k1, &k2);
		result = (namelist_t *)k2;

		if ((result->notbefore > now) || (result->notafter < now)) return NULL;
	}

	return result;
}

void *localhostinfo(char *hostname)
{
	/* Returns a static fake hostrecord */
	static namelist_t *result = NULL;

	if (!result) {
		initialize_hostlist();
		result = (namelist_t *)calloc(1, sizeof(namelist_t));
	}

	strcpy(result->ip, "127.0.0.1");

	if (result->bbhostname) xfree(result->bbhostname);
	result->bbhostname = strdup(hostname);

	if (result->logname) xfree(result->logname);

	result->logname = strdup(hostname);
	{ char *p = result->logname; while ((p = strchr(p, '.')) != NULL) { *p = '_'; } }

	result->preference = 1;
	result->page = pghead;

	if (result->rawentry) xfree(result->rawentry);
	result->rawentry = (char *)malloc(strlen(hostname) + 100);
	sprintf(result->rawentry, "127.0.0.1 %s #", hostname);

	if (result->allelems) xfree(result->allelems);
	result->allelems = strdup("");

	if (result->elems) xfree(result->elems);
	result->elems = (char **)malloc(sizeof(char *));
	result->elems[0] = NULL;

	return result;
}

char *bbh_item(void *hostin, enum bbh_item_t item)
{
	static char *result;
	static char intbuf[10];
	static char *inttxt = NULL;
	static strbuffer_t *rawtxt = NULL;
	char *p;
	namelist_t *host = (namelist_t *)hostin;
	namelist_t *hwalk;

	if (rawtxt == NULL) rawtxt = newstrbuffer(0);
	if (inttxt == NULL) inttxt = (char *)malloc(10);

	if (host == NULL) return NULL;

	switch (item) {
	  case BBH_CLIENTALIAS: 
		  return host->clientname;

	  case BBH_IP:
		  return host->ip;

	  case BBH_CLASS:
		  if (host->classname) return host->classname;
		  else return bbh_find_item(host, item);
		  break;

	  case BBH_OS:
		  if (host->osname) return host->osname;
		  else return bbh_find_item(host, item);
		  break;

	  case BBH_HOSTNAME: 
		  return host->bbhostname;

	  case BBH_PAGENAME:
		  p = strrchr(host->page->pagepath, '/');
		  if (p) return (p+1); else return host->page->pagepath;

	  case BBH_PAGEPATH:
		  return host->page->pagepath;

	  case BBH_PAGETITLE:
		  p = strrchr(host->page->pagetitle, '/');
		  if (p) return (p+1);
		  /* else: Fall through */

	  case BBH_PAGEPATHTITLE:
		  if (strlen(host->page->pagetitle)) return host->page->pagetitle;
		  return "Top Page";

	  case BBH_PAGEINDEX:
		  sprintf(intbuf, "%d", host->pageindex);
		  return intbuf;

	  case BBH_ALLPAGEPATHS:
		  if (rawtxt) clearstrbuffer(rawtxt);
		  hwalk = host;
		  while (hwalk && (strcmp(hwalk->bbhostname, host->bbhostname) == 0)) {
			if (STRBUFLEN(rawtxt) > 0) addtobuffer(rawtxt, ",");
			addtobuffer(rawtxt, hwalk->page->pagepath);
			hwalk = hwalk->next;
		  }
		  return STRBUF(rawtxt);

	  case BBH_GROUPID:
		  return host->groupid;

	  case BBH_DOCURL:
		  p = bbh_find_item(host, item);
		  if (p) {
			if (result) xfree(result);
			result = (char *)malloc(strlen(p) + strlen(host->bbhostname) + 1);
			sprintf(result, p, host->bbhostname);
		  	return result;
		  }
		  else
			return NULL;

	  case BBH_DOWNTIME:
		  if (host->downtime)
			  return host->downtime;
		  else if (host->defaulthost)
			  return host->defaulthost->downtime;
		  else
			  return NULL;

	  case BBH_RAW:
		  if (rawtxt) clearstrbuffer(rawtxt);
		  p = bbh_item_walk(host);
		  while (p) {
			  addtobuffer(rawtxt, nlencode(p));
			  p = bbh_item_walk(NULL);
			  if (p) addtobuffer(rawtxt, "|");
		  }
		  return STRBUF(rawtxt);

	  case BBH_HOLIDAYS:
		  p = bbh_find_item(host, item);
		  if (!p) p = getenv("HOLIDAYS");
		  return p;

	  case BBH_DATA:
		  return host->data;

	  default:
		  return bbh_find_item(host, item);
	}

	return NULL;
}

char *bbh_custom_item(void *hostin, char *key)
{
	int i;
	namelist_t *host = (namelist_t *)hostin;

	i = 0;
	while (host->elems[i] && strncmp(host->elems[i], key, strlen(key))) i++;

	return host->elems[i];
}

enum bbh_item_t bbh_key_idx(char *item)
{
	enum bbh_item_t i;

	bbh_item_list_setup();

	i = 0; while (bbh_item_name[i] && strcmp(bbh_item_name[i], item)) i++;
	return (bbh_item_name[i] ? i : BBH_LAST);
}

char *bbh_item_byname(void *hostin, char *item)
{
	enum bbh_item_t i;
	namelist_t *host = (namelist_t *)hostin;

	i = bbh_key_idx(item);
	return ((i == -1) ? NULL : bbh_item(host, i));
}

char *bbh_item_walk(void *hostin)
{
	static int idx = -1;
	static namelist_t *curhost = NULL;
	char *result;
	namelist_t *host = (namelist_t *)hostin;

	if ((host == NULL) && (idx == -1)) return NULL; /* Programmer failure */
	if (host != NULL) { idx = 0; curhost = host; }

	result = curhost->elems[idx];
	if (result) idx++; else idx = -1;

	return result;
}

int bbh_item_idx(char *value)
{
	int i;

	bbh_item_list_setup();
	i = 0;
	while (bbh_item_key[i] && strncmp(bbh_item_key[i], value, strlen(bbh_item_key[i]))) i++;
	return (bbh_item_key[i] ? i : -1);
}

char *bbh_item_id(enum bbh_item_t idx)
{
	if ((idx >= 0) && (idx < BBH_LAST)) return bbh_item_name[idx];
	return NULL;
}

void *first_host(void)
{
	return namehead;
}

void *next_host(void *currenthost, int wantclones)
{
	namelist_t *walk;

	if (!currenthost) return NULL;

	if (wantclones) return ((namelist_t *)currenthost)->next;

	/* Find the next non-clone record */
	walk = (namelist_t *)currenthost;
	do {
		walk = walk->next;
	} while (walk && (strcmp(((namelist_t *)currenthost)->bbhostname, walk->bbhostname) == 0));

	return walk;
}

void bbh_set_item(void *hostin, enum bbh_item_t item, void *value)
{
	namelist_t *host = (namelist_t *)hostin;

	switch (item) {
	  case BBH_CLASS:
		if (host->classname) xfree(host->classname);
		host->classname = strdup((char *)value);
		break;

	  case BBH_OS:
		if (host->osname) xfree(host->osname);
		host->osname = strdup((char *)value);
		break;

	  case BBH_DATA:
		host->data = value;
		break;

	  case BBH_CLIENTALIAS:
		/*
		 * FIXME: Small mem. leak here - we should run "rebuildhosttree", but that is heavy.
		 * Doing this "free" kills the tree structure, since we free one of the keys.
		 *
		 * if (host->clientname && (host->bbhostname != host->clientname) && (host->clientname != bbh_find_item(host, BBH_CLIENTALIAS)) xfree(host->clientname);
		 */
		host->clientname = strdup((char *)value);
		rbtInsert(rbclients, host->clientname, host);
		break;

	  default:
		break;
	}
}


char *bbh_item_multi(void *hostin, enum bbh_item_t item)
{
	namelist_t *host = (namelist_t *)hostin;
	static namelist_t *keyhost = NULL, *curhost = NULL;

	if (item == BBH_LAST) return NULL;

	if ((host == NULL) && (keyhost == NULL)) return NULL; /* Programmer failure */

	if (host != NULL) 
		curhost = keyhost = host;
	else {
		curhost = curhost->next;
		if (!curhost || (strcmp(curhost->bbhostname, keyhost->bbhostname) != 0))
			curhost = keyhost = NULL; /* End of hostlist */
	}

	return bbh_item(curhost, item);
}

#ifdef STANDALONE

int main(int argc, char *argv[])
{
	int argi;
	namelist_t *h;
	char *val;

	load_hostnames(argv[1], NULL, get_fqdn());

	for (argi = 2; (argi < argc); argi++) {
		char s[1024];
		char *p;
		char *hname;
		char hostip[IP_ADDR_STRLEN];

handlehost:
		hname = knownhost(argv[argi], hostip, GH_IGNORE);
		if (hname == NULL) {
			printf("Unknown host '%s'\n", argv[argi]);
			continue;
		}
		if (strcmp(hname, argv[argi])) {
			printf("Using canonical name '%s'\n", hname);
		}
		h = hostinfo(hname);

		if (h == NULL) { printf("Host %s not found\n", argv[argi]); continue; }

		val = bbh_item_walk(h);
		printf("Entry for host %s\n", h->bbhostname);
		while (val) {
			printf("\t%s\n", val);
			val = bbh_item_walk(NULL);
		}

		do {
			*s = '\0';
			printf("Pick item:"); fflush(stdout); fgets(s, sizeof(s), stdin);
			p = strchr(s, '\n'); if (p) *p = '\0';
			if (*s == '!') {
				load_hostnames(argv[1], NULL, get_fqdn());
				/* Must restart! The "h" handle is no longer valid. */
				goto handlehost;
			}
			else if (*s == '>') {
				val = bbh_item_multi(h, BBH_PAGEPATH);
				while (val) {
					printf("\t%s value is: '%s'\n", s, val);
					val = bbh_item_multi(NULL, BBH_PAGEPATH);
				}
			}
			else if (strncmp(s, "set ", 4) == 0) {
				bbh_set_item(h, BBH_DATA, strdup(s+4));
			}
			else if (*s) {
				val = bbh_item_byname(h, s);
				if (val) printf("\t%s value is: '%s'\n", s, val);
				else printf("\t%s not found\n", s);
			}
		} while (*s);
	}

	return 0;
}

#endif

