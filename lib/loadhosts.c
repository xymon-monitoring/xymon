/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the hosts.cfg  */
/* file and keeping track of what hosts are known, their aliases and planned  */
/* downtime settings etc.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/


static char rcsid[] = "$Id$";

#include "config.h"
#ifdef HAVE_BINARY_TREE
#define _GNU_SOURCE
#include <search.h>
#endif
#include "tree.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#include "libxymon.h"

char *location = NULL;	/* XYMONNETWORK value - where we are now */
int testuntagged = 0;	/* If location is set, test hosts w/o a NET: ? */
char **locations;	/* Any XYMONNETWORKS specified to include? */
char **exlocations;	/* Any XYMONEXNETWORKS specified to exclude? */
int haslocations = 0;
int hasexlocations = 0;

static pagelist_t *pghead = NULL;
static namelist_t *namehead = NULL, *nametail = NULL;
static namelist_t *defaulthost = NULL;
static char *xmh_item_key[XMH_LAST];
static char *xmh_item_name[XMH_LAST];
static int xmh_item_isflag[XMH_LAST];
static int configloaded = 0;
static void * rbhosts;
static void * rbclients;

static void xmh_item_list_setup(void)
{
	static int setupdone = 0;
	int i;
	enum xmh_item_t bi;

	if (setupdone) return;

	/* Doing it this way makes sure the index matches the value */
	setupdone = 1;
	memset(xmh_item_key, 0, sizeof(xmh_item_key));
	memset(xmh_item_name, 0, sizeof(xmh_item_key));
	memset(xmh_item_isflag, 0, sizeof(xmh_item_isflag));
	xmh_item_key[XMH_NET]                  = "NET:";
	xmh_item_name[XMH_NET]                 = "XMH_NET";
	xmh_item_key[XMH_DISPLAYNAME]          = "NAME:";
	xmh_item_name[XMH_DISPLAYNAME]         = "XMH_DISPLAYNAME";
	xmh_item_key[XMH_CLIENTALIAS]          = "CLIENT:";
	xmh_item_name[XMH_CLIENTALIAS]         = "XMH_CLIENTALIAS";
	xmh_item_key[XMH_COMMENT]              = "COMMENT:";
	xmh_item_name[XMH_COMMENT]             = "XMH_COMMENT";
	xmh_item_key[XMH_DESCRIPTION]          = "DESCR:";
	xmh_item_name[XMH_DESCRIPTION]         = "XMH_DESCRIPTION";
	xmh_item_key[XMH_DOCURL]               = "DOC:";
	xmh_item_name[XMH_DOCURL]              = "XMH_DOCURL";
	xmh_item_key[XMH_NK]                   = "NK:";
	xmh_item_name[XMH_NK]                  = "XMH_NK";
	xmh_item_key[XMH_NKTIME]               = "NKTIME=";
	xmh_item_name[XMH_NKTIME]              = "XMH_NKTIME";
	xmh_item_key[XMH_TRENDS]               = "TRENDS:";
	xmh_item_name[XMH_TRENDS]              = "XMH_TRENDS";
	xmh_item_key[XMH_WML]                  = "WML:";
	xmh_item_name[XMH_WML]                 = "XMH_WML";
	xmh_item_key[XMH_NOPROP]               = "NOPROP:";
	xmh_item_name[XMH_NOPROP]              = "XMH_NOPROP";
	xmh_item_key[XMH_NOPROPRED]            = "NOPROPRED:";
	xmh_item_name[XMH_NOPROPRED]           = "XMH_NOPROPRED";
	xmh_item_key[XMH_NOPROPYELLOW]         = "NOPROPYELLOW:";
	xmh_item_name[XMH_NOPROPYELLOW]        = "XMH_NOPROPYELLOW";
	xmh_item_key[XMH_NOPROPPURPLE]         = "NOPROPPURPLE:";
	xmh_item_name[XMH_NOPROPPURPLE]        = "XMH_NOPROPPURPLE";
	xmh_item_key[XMH_NOPROPACK]            = "NOPROPACK:";
	xmh_item_name[XMH_NOPROPACK]           = "XMH_NOPROPACK";
	xmh_item_key[XMH_REPORTTIME]           = "REPORTTIME=";
	xmh_item_name[XMH_REPORTTIME]          = "XMH_REPORTTIME";
	xmh_item_key[XMH_WARNPCT]              = "WARNPCT:";
	xmh_item_name[XMH_WARNPCT]             = "XMH_WARNPCT";
	xmh_item_key[XMH_WARNSTOPS]            = "WARNSTOPS:";
	xmh_item_name[XMH_WARNSTOPS]           = "XMH_WARNSTOPS";
	xmh_item_key[XMH_DOWNTIME]             = "DOWNTIME=";
	xmh_item_name[XMH_DOWNTIME]            = "XMH_DOWNTIME";
	xmh_item_key[XMH_SSLDAYS]              = "ssldays=";
	xmh_item_name[XMH_SSLDAYS]             = "XMH_SSLDAYS";
	xmh_item_key[XMH_SSLMINBITS]           = "sslbits=";
	xmh_item_name[XMH_SSLMINBITS]          = "XMH_SSLMINBITS";
	xmh_item_key[XMH_DEPENDS]              = "depends=";
	xmh_item_name[XMH_DEPENDS]             = "XMH_DEPENDS";
	xmh_item_key[XMH_BROWSER]              = "browser=";
	xmh_item_name[XMH_BROWSER]             = "XMH_BROWSER";
	xmh_item_key[XMH_HTTPHEADERS]              = "httphdr=";
	xmh_item_name[XMH_HTTPHEADERS]             = "XMH_HTTPHEADERS";
	xmh_item_key[XMH_HOLIDAYS]             = "holidays=";
	xmh_item_name[XMH_HOLIDAYS]            = "XMH_HOLIDAYS";
	xmh_item_key[XMH_DELAYRED]             = "delayred=";
	xmh_item_name[XMH_DELAYRED]            = "XMH_DELAYRED";
	xmh_item_key[XMH_DELAYYELLOW]          = "delayyellow=";
	xmh_item_name[XMH_DELAYYELLOW]         = "XMH_DELAYYELLOW";
	xmh_item_key[XMH_FLAG_NOINFO]          = "noinfo";
	xmh_item_name[XMH_FLAG_NOINFO]         = "XMH_FLAG_NOINFO";
	xmh_item_key[XMH_FLAG_NOTRENDS]        = "notrends";
	xmh_item_name[XMH_FLAG_NOTRENDS]       = "XMH_FLAG_NOTRENDS";
	xmh_item_key[XMH_FLAG_NOCLIENT]        = "noclient";
	xmh_item_name[XMH_FLAG_NOCLIENT]       = "XMH_FLAG_NOCLIENT";
	xmh_item_key[XMH_FLAG_NODISP]          = "nodisp";
	xmh_item_name[XMH_FLAG_NODISP]         = "XMH_FLAG_NODISP";
	xmh_item_key[XMH_FLAG_NONONGREEN]      = "nonongreen";
	xmh_item_name[XMH_FLAG_NONONGREEN]     = "XMH_FLAG_NONONGREEN";
	xmh_item_key[XMH_FLAG_NOBB2]           = "nobb2";
	xmh_item_name[XMH_FLAG_NOBB2]          = "XMH_FLAG_NOBB2";
	xmh_item_key[XMH_FLAG_PREFER]          = "prefer";
	xmh_item_name[XMH_FLAG_PREFER]         = "XMH_FLAG_PREFER";
	xmh_item_key[XMH_FLAG_NOSSLCERT]       = "nosslcert";
	xmh_item_name[XMH_FLAG_NOSSLCERT]      = "XMH_FLAG_NOSSLCERT";
	xmh_item_key[XMH_FLAG_TRACE]           = "trace";
	xmh_item_name[XMH_FLAG_TRACE]          = "XMH_FLAG_TRACE";
	xmh_item_key[XMH_FLAG_NOTRACE]         = "notrace";
	xmh_item_name[XMH_FLAG_NOTRACE]        = "XMH_FLAG_NOTRACE";
	xmh_item_key[XMH_FLAG_NOCONN]          = "noconn";
	xmh_item_name[XMH_FLAG_NOCONN]         = "XMH_FLAG_NOCONN";
	xmh_item_key[XMH_FLAG_NOPING]          = "noping";
	xmh_item_name[XMH_FLAG_NOPING]         = "XMH_FLAG_NOPING";
	xmh_item_key[XMH_FLAG_DIALUP]          = "dialup";
	xmh_item_name[XMH_FLAG_DIALUP]         = "XMH_FLAG_DIALUP";
	xmh_item_key[XMH_FLAG_TESTIP]          = "testip";
	xmh_item_name[XMH_FLAG_TESTIP]         = "XMH_FLAG_TESTIP";
	xmh_item_key[XMH_FLAG_LDAPFAILYELLOW]  = "ldapyellowfail";
	xmh_item_name[XMH_FLAG_LDAPFAILYELLOW] = "XMH_FLAG_LDAPFAILYELLOW";
	xmh_item_key[XMH_FLAG_NOCLEAR]         = "NOCLEAR";
	xmh_item_name[XMH_FLAG_NOCLEAR]        = "XMH_FLAG_NOCLEAR";
	xmh_item_key[XMH_FLAG_HIDEHTTP]        = "HIDEHTTP";
	xmh_item_name[XMH_FLAG_HIDEHTTP]       = "XMH_FLAG_HIDEHTTP";
	xmh_item_key[XMH_PULLDATA]             = "PULLDATA";
	xmh_item_name[XMH_PULLDATA]            = "XMH_PULLDATA";
	xmh_item_key[XMH_NOFLAP]               = "NOFLAP";
	xmh_item_name[XMH_NOFLAP]              = "XMH_NOFLAP";
	xmh_item_key[XMH_FLAG_MULTIHOMED]      = "MULTIHOMED";
	xmh_item_name[XMH_FLAG_MULTIHOMED]     = "XMH_MULTIHOMED";
	xmh_item_key[XMH_FLAG_HTTP_HEADER_MATCH]             = "headermatch";
	xmh_item_name[XMH_FLAG_HTTP_HEADER_MATCH]            = "XMH_FLAG_HTTP_HEADER_MATCH";
	xmh_item_key[XMH_FLAG_SNI]             = "sni";			// Enable SNI (Server name Indication) for TLS requests
	xmh_item_name[XMH_FLAG_SNI]            = "XMH_FLAG_SNI";
	xmh_item_key[XMH_FLAG_NOSNI]           = "nosni";		// Disable SNI (Server name Indication) for TLS requests
	xmh_item_name[XMH_FLAG_NOSNI]          = "XMH_FLAG_NOSNI";
	xmh_item_key[XMH_LDAPLOGIN]            = "ldaplogin=";
	xmh_item_name[XMH_LDAPLOGIN]           = "XMH_LDAPLOGIN";
	xmh_item_key[XMH_CLASS]                = "CLASS:";
	xmh_item_name[XMH_CLASS]               = "XMH_CLASS";
	xmh_item_key[XMH_OS]                   = "OS:";
	xmh_item_name[XMH_OS]                  = "XMH_OS";
	xmh_item_key[XMH_NOCOLUMNS]            = "NOCOLUMNS:";
	xmh_item_name[XMH_NOCOLUMNS]           = "XMH_NOCOLUMNS";
	xmh_item_key[XMH_NOTBEFORE]            = "NOTBEFORE:";
	xmh_item_name[XMH_NOTBEFORE]           = "XMH_NOTBEFORE";
	xmh_item_key[XMH_NOTAFTER]             = "NOTAFTER:";
	xmh_item_name[XMH_NOTAFTER]            = "XMH_NOTAFTER";
	xmh_item_key[XMH_COMPACT]              = "COMPACT:";
	xmh_item_name[XMH_COMPACT]             = "XMH_COMPACT";
	xmh_item_key[XMH_INTERFACES]           = "INTERFACES:";
	xmh_item_name[XMH_INTERFACES]          = "XMH_INTERFACES";
	xmh_item_key[XMH_ACCEPT_ONLY]          = "ACCEPTONLY:";
	xmh_item_name[XMH_ACCEPT_ONLY]         = "XMH_ACCEPT_ONLY";

	xmh_item_name[XMH_IP]                  = "XMH_IP";
	xmh_item_name[XMH_CLIENTALIAS]         = "XMH_CLIENTALIAS";
	xmh_item_name[XMH_HOSTNAME]            = "XMH_HOSTNAME";
	xmh_item_name[XMH_PAGENAME]            = "XMH_PAGENAME";
	xmh_item_name[XMH_PAGEPATH]            = "XMH_PAGEPATH";
	xmh_item_name[XMH_PAGETITLE]           = "XMH_PAGETITLE";
	xmh_item_name[XMH_PAGEPATHTITLE]       = "XMH_PAGEPATHTITLE";
	xmh_item_name[XMH_ALLPAGEPATHS]        = "XMH_ALLPAGEPATHS";
	xmh_item_name[XMH_GROUPID]             = "XMH_GROUPID";
	xmh_item_name[XMH_DGNAME]              = "XMH_DGNAME";
	xmh_item_name[XMH_PAGEINDEX]           = "XMH_PAGEINDEX";
	xmh_item_name[XMH_RAW]                 = "XMH_RAW";
	xmh_item_name[XMH_DATA]                = "XMH_DATA";

	i = 0; while (xmh_item_key[i]) i++;
	if (i != XMH_IP) {
		errprintf("ERROR: Setup failure in xmh_item_key position %d\n", i);
	}

	for (bi = 0; (bi < XMH_LAST); bi++) 
		if (xmh_item_name[bi]) xmh_item_isflag[bi] = (strncmp(xmh_item_name[bi], "XMH_FLAG_", 9) == 0);
}


static char *xmh_find_item(namelist_t *host, enum xmh_item_t item)
{
	int i;
	char *result;

	if (item == XMH_LAST) return NULL;	/* Unknown item requested */
	if (host == NULL) return NULL;	/* Unknown item requested */

	xmh_item_list_setup();
	i = 0;
	while (host->elems[i] && strncasecmp(host->elems[i], xmh_item_key[item], strlen(xmh_item_key[item]))) i++;
	result = (host->elems[i] ? (host->elems[i] + strlen(xmh_item_key[item])) : NULL);

	/* Handle the LARRD: tag in Xymon 4.0.4 and earlier */
	if (!result && (item == XMH_TRENDS)) {
		i = 0;
		while (host->elems[i] && strncasecmp(host->elems[i], "LARRD:", 6)) i++;
		result = (host->elems[i] ? (host->elems[i] + 6) : NULL);
	}

	if (result || !host->defaulthost || (strcasecmp(host->hostname, ".default.") == 0)) {
		if (xmh_item_isflag[item]) {
			return (result ? xmh_item_key[item] : NULL);
		}
		else
			return result;
	}
	else
		return xmh_find_item(host->defaulthost, item);
}


void freenamelist(namelist_t *rec)
{
	dbgprintf(" - freenamelist called on hostname: %s at %p\n", (rec ? rec->hostname ? rec->hostname : "record, but hostname null" : "null record"), rec);
	if (rec == NULL) return;
	/* clientname should not be freed, since it's just a pointer into the elems-array (or *rec->hostname) */
	if (rec->ip != NULL)		xfree(rec->ip);
	if (rec->hostname != NULL)	xfree(rec->hostname);
	if (rec->logname != NULL)	xfree(rec->logname);
	if (rec->groupid != NULL)	xfree(rec->groupid);
	if (rec->dgname != NULL)	xfree(rec->dgname);
	if (rec->classname != NULL)	xfree(rec->classname);
	if (rec->osname != NULL)	xfree(rec->osname);
	if (rec->allelems != NULL)	xfree(rec->allelems);
	if (rec->elems != NULL)		xfree(rec->elems);
	if (rec->data != NULL)		xfree(rec->data);
	return;
}

void initialize_hostlist(void)
{
	dbgprintf(" -> initialize_hostlist\n");
	while (defaulthost) {
		namelist_t *walk = defaulthost;
		defaulthost = defaulthost->defaulthost;

		freenamelist(walk);
		xfree(walk);
	}

	dbgprintf("  - freeing name list (namehead)\n");
	while (namehead) {
		namelist_t *walk = namehead;

		namehead = namehead->next;
		freenamelist(walk);
		xfree(walk);
	}
	nametail = NULL;

	dbgprintf("  - freeing page list (pghead)\n");
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

	dbgprintf(" <- initialize_hostlist\n");
}

#ifdef HAVE_BINARY_TREE
void xtree_delete_hostname (treerec_t *hosttree)
{
	if (hosttree != NULL) freenamelist((namelist_t *)hosttree->userdata);
}
#endif
 
void destroy_hosttree(void)
{
#ifdef HAVE_BINARY_TREE
	dbgprintf(" -> destroy_hosttree (posix)\n");
	if (rbhosts != NULL) {
		// tdestroy(((xtree_t *)rbhosts)->root, (__free_fn_t)xtree_delete_hostname); /* Do not do a namelist_t delete */
		// tdestroy(((xtree_t *)rbhosts)->root, (__free_fn_t)xtree_empty);
		xtreeDestroy(rbhosts);
		// free(rbhosts);
	}
	if (rbclients != NULL) {
		// tdestroy(((xtree_t *)rbclients)->root, (__free_fn_t)xtree_empty);
		xtreeDestroy(rbclients);
		// free(rbclients);
	}
#else
	dbgprintf(" -> destroy_hosttree\n");
	if (rbhosts != NULL) xtreeDestroy(rbhosts);
	if (rbclients != NULL) xtreeDestroy(rbclients);
#endif
	dbgprintf(" <- destroy_hosttree\n");
	return;
}

static void build_hosttree(void)
{
	namelist_t *walk;
	xtreeStatus_t status;
	char *tstr;

	dbgprintf(" -> build_hosttree\n");
	destroy_hosttree();

	rbhosts = xtreeNew(strcasecmp);
	rbclients = xtreeNew(strcasecmp);

	for (walk = namehead; (walk); walk = walk->next) {
		if (walk->hostname == NULL) { errprintf(" BUG: What? -> build_hosttree not trying to insert a hostrec with no hostname\n"); continue; }
		status = xtreeAdd(rbhosts, walk->hostname, walk);
		if (walk->clientname) xtreeAdd(rbclients, walk->clientname, walk);

		switch (status) {
		  case XTREE_STATUS_OK:
		  case XTREE_STATUS_DUPLICATE_KEY:
			break;
		  case XTREE_STATUS_MEM_EXHAUSTED:
			errprintf("loadhosts:build_hosttree - insert into tree failed (out of memory)\n");
			break;
		  default:
			errprintf("loadhosts:build_hosttree - insert into tree failed code %d\n", status);
			break;
		}

		tstr = xmh_item(walk, XMH_NOTBEFORE);
		walk->notbefore = (tstr ? timestr2timet(tstr) : 0);
		if (walk->notbefore == -1) walk->notbefore = 0;

		tstr = xmh_item(walk, XMH_NOTAFTER);
		walk->notafter = (tstr ? timestr2timet(tstr) : INT_MAX);
		if (walk->notafter == -1) walk->notafter = INT_MAX;
	}
	dbgprintf(" <- build_hosttree\n");
}

#include "loadhosts_file.c"
#include "loadhosts_net.c"

void load_locations(void)
{
	static int setup_done = 0;
	char *oneloc, *p;

	dbgprintf(" -> load_locations\n");

	if (setup_done++) return;

	if ((testuntagged == 0) && (strcasecmp(xgetenv("TESTUNTAGGED"), "TRUE") == 0)) {
			dbgprintf(" - Enabling testing untagged\n");
			testuntagged = 1;
	}

	/* Where this system physically is */
	if (getenv("XYMONNETWORK")) location = getenv("XYMONNETWORK");

	/* Networks to include. If not set at all, copy our location into it for compatibility */
	if (!getenv("XYMONNETWORKS")) {
		if (( (!location) || (!strlen(location)) ) && 
			getenv("BBLOCATION") && strlen(getenv("BBLOCATION"))) location=getenv("BBLOCATION");

		if (location && strlen(location)) {
			dbgprintf(" adding XYMONNETWORK=%s to otherwise empty XYMONNETWORKS list\n", location);

			locations = malloc(2 * sizeof(char *));
			haslocations = 1;
			locations[0] = strdup(location);
			locations[1] = NULL;
		}
	}
	else {
		locations = malloc(sizeof(char *));

		p = strdup(xgetenv("XYMONNETWORKS"));
		oneloc = strtok(p, ",");
		while (oneloc) {
			locations = realloc(locations, (sizeof(char *)) * (haslocations + 2));
			locations[haslocations++] = strdup(oneloc);
			dbgprintf(" adding XYMONNETWORKS location %s to list (%d total)\n", oneloc, haslocations);
			oneloc = strtok(NULL, ",");
		}
		locations[haslocations] = NULL;
		xfree(p);
	}


	/* Networks to exclude */
	if (xgetenv("XYMONEXNETWORKS") && (strlen(getenv("XYMONEXNETWORKS")) > 0)) {
		exlocations = malloc(sizeof(char *));

		p = strdup(xgetenv("XYMONEXNETWORKS"));
		oneloc = strtok(p, ",");
		while (oneloc) {
			exlocations = realloc(exlocations, (sizeof(char *)) * (hasexlocations + 2));
			exlocations[hasexlocations++] = strdup(oneloc);
			dbgprintf(" adding XYMONEXNETWORKS location %s to list (%d total)\n", oneloc, hasexlocations);
			oneloc = strtok(NULL, ",");
		}
		exlocations[hasexlocations] = NULL;
		xfree(p);
	}

	/* Use a blank location as a flag */
	if (location && (!strlen(location)) ) xfree(location);

	dbgprintf("  - Current location: %s\n", textornull(location) );
	dbgprintf("  - Included locations (%d): %s\n", haslocations, 
	  textornull(	(location && (haslocations == 1) && (!getenv("XYMONNETWORKS")) )  ? location : getenv("XYMONNETWORKS") ));
	dbgprintf("  - Excluded locations (%d): %s\n", hasexlocations, textornull(getenv("XYMONEXNETWORKS")) );
	dbgprintf("  - Testing untagged hosts: %sabled\n", (testuntagged ? "en" : "dis") );
	dbgprintf(" <- load_locations\n");

	return;
}


int oklocation_host(char *netlocation)
{
	/*
	 * Make sure it's a host in our current network, or not in an excluded one.
	 * First, check for matching exclude entries, then matching include entries,
	 * then allow it if
	 *	a) we're not filtering based on network at all, or
	 *	b) the host has no network and testuntagged was specified
	 */
	int i;

	/* 
	 * TODO 4.x: prefix matching requires a strlen H * L times per run.
	 * Better to create a real struct, store the length, and pre-lowercase all
	 * fields once hosts.cfg case resolution is finished.
	 */

	if (netlocation != NULL) {
		if (hasexlocations) {
			for (i=0; (i < hasexlocations); i++) {
				if (strcasecmp(netlocation, exlocations[i]) == 0) return 0;   /* bail */
			}
			// dbgprintf(" -- host in net:%s; NOT found in %d specified XYMONEXNETWORKS\n", netlocation, hasexlocations);
		}

		if (haslocations) {
			for (i=0; (i < haslocations); i++) {
				if (strcasecmp(netlocation, locations[i]) == 0) return 1;   /* matches */
			}
			// dbgprintf(" -- host in net:%s; NOT found in %d specified XYMONNETWORKS\n", netlocation, haslocations);
		}
	}

	return ((haslocations == 0) ||				/* No XYMONNETWORKS == do all */
		(testuntagged && (netlocation == NULL)));	/* No NET: tag for this host */
 }



char *knownhost(char *hostname, char **hostip, enum ghosthandling_t ghosthandling)
{
	/*
	 * ghosthandling = GH_ALLOW  : Default BB method (case-sensitive, no logging, keep ghosts)
	 * ghosthandling = GH_IGNORE : Case-insensitive, no logging, drop ghosts
	 * ghosthandling = GH_LOG    : Case-insensitive, log ghosts, drop ghosts
	 * ghosthandling = GH_MATCH  : Like GH_LOG, but try to match unknown names against known hosts
	 */
	xtreePos_t hosthandle;
	namelist_t *walk = NULL;
	static char *result = NULL;
	time_t now = getcurrenttime(NULL);

	if (result) xfree(result);
	result = NULL;
	if (hostip) *hostip = NULL;

	if (hivalhost) {
		*hostip = '\0';

		if (!hivalbuf || (*hivalbuf == '\0')) return NULL;

		result = (strcasecmp(hivalhost, hostname) == 0) ? strdup(hivalhost) : NULL;
		if (!result && hivals[XMH_CLIENTALIAS]) {
			result = (strcasecmp(hivals[XMH_CLIENTALIAS], hostname) == 0) ? strdup(hivalhost) : NULL;
		}

		if (result && hivals[XMH_IP] && hostip) *hostip = hivals[XMH_IP];

		return result;
	}


	/* Find the host in the normal hostname list */
	hosthandle = xtreeFind(rbhosts, hostname);
	if (hosthandle != xtreeEnd(rbhosts)) {
		walk = (namelist_t *)xtreeData(rbhosts, hosthandle);
	}
	else {
		/* Not found - lookup in the client alias list */
		hosthandle = xtreeFind(rbclients, hostname);
		if (hosthandle != xtreeEnd(rbclients)) {
			walk = (namelist_t *)xtreeData(rbclients, hosthandle);
		}
	}

	if (walk) {
		/*
		 * Force our version of the hostname. Done here so CLIENT works always.
		 */
		*hostip = walk->ip;
		result = strdup(walk->hostname);
	}
	else {
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

	if (hivalhost) {
		int result;
		char *hvh_logname, *p;

		hvh_logname = strdup(hivalhost);
		p = hvh_logname; while ((p = strchr(p, '.')) != NULL) { *p = '_'; }

		result = (strcasecmp(hvh_logname, logdir) == 0);

		xfree(hvh_logname);
		return result;
	}

	/* Find the host */
	/* Must do the linear string search, since the tree is indexed by the hostname, not logname */
	for (walk = namehead; (walk && (strcasecmp(walk->logname, logdir) != 0)); walk = walk->next);

	return (walk != NULL);
}

void *hostinfo(char *hostname)
{
	xtreePos_t hosthandle;
	namelist_t *result = NULL;
	time_t now = getcurrenttime(NULL);

	if (hivalhost) {
		return (strcasecmp(hostname, hivalhost) == 0) ? &hival_hostinfo : NULL;
	}

	if (!configloaded) {
		load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
		configloaded = 1;
	}

	hosthandle = xtreeFind(rbhosts, hostname);
	if (hosthandle != xtreeEnd(rbhosts)) {
		result = (namelist_t *)xtreeData(rbhosts, hosthandle);
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

	result->ip = strdup("127.0.0.1");

	if (result->hostname) xfree(result->hostname);
	result->hostname = strdup(hostname);

	if (result->logname) xfree(result->logname);

	result->logname = strdup(hostname);
	{ char *p = result->logname; while ((p = strchr(p, '.')) != NULL) { *p = '_'; } }

	result->preference = 1;
	result->page = pghead;

	if (result->allelems) xfree(result->allelems);
	result->allelems = strdup("");

	if (result->elems) xfree(result->elems);
	result->elems = (char **)malloc(sizeof(char *));
	result->elems[0] = NULL;

	return result;
}

char *xmh_item(void *hostin, enum xmh_item_t item)
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

	if (host == &hival_hostinfo) return hivals[item];

	switch (item) {
	  case XMH_CLIENTALIAS: 
		  return host->clientname;

	  case XMH_IP:
		  return host->ip;

	  case XMH_CLASS:
		  if (host->classname) return host->classname;
		  else return xmh_find_item(host, item);
		  break;

	  case XMH_OS:
		  if (host->osname) return host->osname;
		  else return xmh_find_item(host, item);
		  break;

	  case XMH_HOSTNAME: 
		  return host->hostname;

	  case XMH_PAGENAME:
		  p = strrchr(host->page->pagepath, '/');
		  if (p) return (p+1); else return host->page->pagepath;

	  case XMH_PAGEPATH:
		  return host->page->pagepath;

	  case XMH_PAGETITLE:
		  p = strrchr(host->page->pagetitle, '/');
		  if (p) return (p+1);
		  /* else: Fall through */

	  case XMH_PAGEPATHTITLE:
		  if (strlen(host->page->pagetitle)) return host->page->pagetitle;
		  return "Top Page";

	  case XMH_PAGEINDEX:
		  sprintf(intbuf, "%d", host->pageindex);
		  return intbuf;

	  case XMH_ALLPAGEPATHS:
		  if (rawtxt) clearstrbuffer(rawtxt);
		  hwalk = host;
		  while (hwalk && (strcmp(hwalk->hostname, host->hostname) == 0)) {
			if (STRBUFLEN(rawtxt) > 0) addtobuffer(rawtxt, ",");
			addtobuffer(rawtxt, hwalk->page->pagepath);
			hwalk = hwalk->next;
		  }
		  return STRBUF(rawtxt);

	  case XMH_GROUPID:
		  return host->groupid;

	  case XMH_DGNAME:
		  return host->dgname;

	  case XMH_DOCURL:
		  p = xmh_find_item(host, item);
		  if (p) {
			if (result) xfree(result);
			result = (char *)malloc(strlen(p) + strlen(host->hostname) + 1);
			sprintf(result, p, host->hostname);
		  	return result;
		  }
		  else
			return NULL;

	  case XMH_DOWNTIME:
		  if (host->downtime)
			  return host->downtime;
		  else if (host->defaulthost)
			  return host->defaulthost->downtime;
		  else
			  return NULL;

	  case XMH_RAW:
		  if (rawtxt) clearstrbuffer(rawtxt);
		  p = xmh_item_walk(host);
		  while (p) {
			  addtobuffer(rawtxt, nlencode(p));
			  p = xmh_item_walk(NULL);
			  if (p) addtobuffer(rawtxt, "|");
		  }
		  return STRBUF(rawtxt);

	  case XMH_HOLIDAYS:
		  p = xmh_find_item(host, item);
		  if (!p) p = getenv("HOLIDAYS");
		  return p;

	  case XMH_DATA:
		  return host->data;

	  case XMH_FLAG_NONONGREEN:
	  case XMH_FLAG_NOBB2:
		  p = xmh_find_item(host, XMH_FLAG_NONONGREEN);
		  if (p == NULL) p = xmh_find_item(host, XMH_FLAG_NOBB2);
		  return p;

	  case XMH_NOFLAP:
		  /* special - can be 'noflap=test1,test2' or just 'noflap' */
		  p = xmh_find_item(host, XMH_NOFLAP);
		  if ((p != NULL) && (*(p) == '\0')) p = xmh_item_key[XMH_NOFLAP];	/* mirror flag semantics */
		  return p;

	  case XMH_PULLDATA:
		  /* special - can be 'pulldata=IP:PORT' or just 'pulldata' */
		  p = xmh_find_item(host, XMH_PULLDATA);
		  if ((p != NULL) && (*(p) == '\0')) p = xmh_item_key[XMH_PULLDATA];	/* mirror flag semantics */
		  return p;

	  default:
		  return xmh_find_item(host, item);
	}

	return NULL;
}

char *xmh_custom_item(void *hostin, char *key)
{
	int i;
	namelist_t *host = (namelist_t *)hostin;

	i = 0;
	while (host->elems[i] && strncmp(host->elems[i], key, strlen(key))) i++;

	return host->elems[i];
}

enum xmh_item_t xmh_key_idx(char *item)
{
	enum xmh_item_t i;

	xmh_item_list_setup();

	i = 0; while (xmh_item_name[i] && strcmp(xmh_item_name[i], item)) i++;
	return (xmh_item_name[i] ? i : XMH_LAST);
}

char *xmh_item_byname(void *hostin, char *item)
{
	enum xmh_item_t i;
	namelist_t *host = (namelist_t *)hostin;

	i = xmh_key_idx(item);
	return ((i == -1) ? NULL : xmh_item(host, i));
}

char *xmh_item_walk(void *hostin)
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

int xmh_item_idx(char *value)
{
	int i;

	xmh_item_list_setup();
	i = 0;
	while (xmh_item_key[i] && strncmp(xmh_item_key[i], value, strlen(xmh_item_key[i]))) i++;
	return (xmh_item_key[i] ? i : -1);
}

char *xmh_item_id(enum xmh_item_t idx)
{
	if ((idx >= 0) && (idx < XMH_LAST)) return xmh_item_name[idx];
	return NULL;
}

void *first_host(void)
{
	return (hivalhost ? &hival_hostinfo : namehead);
}

void *next_host(void *currenthost, int wantclones)
{
	namelist_t *walk;

	if (!currenthost || (currenthost == &hival_hostinfo)) return NULL;

	if (wantclones) return ((namelist_t *)currenthost)->next;

	/* Find the next non-clone record */
	walk = (namelist_t *)currenthost;
	do {
		walk = walk->next;
	} while (walk && (strcmp(((namelist_t *)currenthost)->hostname, walk->hostname) == 0));

	return walk;
}

void xmh_set_item(void *hostin, enum xmh_item_t item, void *value)
{
	namelist_t *host = (namelist_t *)hostin;

	if (host == &hival_hostinfo) {
		switch (item) {
		  case XMH_CLASS:
		  case XMH_OS:
		  case XMH_CLIENTALIAS:
			hivals[item] = strdup((char *)value);
			break;
		  case XMH_DATA:
			hivals[item] = (char *)value;
			break;
		  default:
			break;
		}

		return;
	}

	switch (item) {
	  case XMH_CLASS:
		if (host->classname) xfree(host->classname);
		host->classname = strdup((char *)value);
		break;

	  case XMH_OS:
		if (host->osname) xfree(host->osname);
		host->osname = strdup((char *)value);
		break;

	  case XMH_DATA:
		host->data = value;
		break;

	  case XMH_CLIENTALIAS:
		/*
		 * FIXME: Small mem. leak here - we should run "rebuildhosttree", but that is heavy.
		 * Doing this "free" kills the tree structure, since we free one of the keys.
		 *
		 * if (host->clientname && (host->hostname != host->clientname) && (host->clientname != xmh_find_item(host, XMH_CLIENTALIAS)) xfree(host->clientname);
		 */
		host->clientname = strdup((char *)value);
		xtreeAdd(rbclients, host->clientname, host);
		break;

	  default:
		break;
	}
}


char *xmh_item_multi(void *hostin, enum xmh_item_t item)
{
	namelist_t *host = (namelist_t *)hostin;
	static namelist_t *keyhost = NULL, *curhost = NULL;

	if (item == XMH_LAST) return NULL;

	if ((host == NULL) && (keyhost == NULL)) return NULL; /* Programmer failure */

	if (host != NULL) 
		curhost = keyhost = host;
	else {
		curhost = curhost->next;
		if (!curhost || (strcmp(curhost->hostname, keyhost->hostname) != 0))
			curhost = keyhost = NULL; /* End of hostlist */
	}

	return xmh_item(curhost, item);
}

#ifdef STANDALONE

int main(int argc, char *argv[])
{
	int argi;
	namelist_t *h;
	char *val;

	if (strcmp(argv[1], "@") == 0) {
		load_hostinfo(argv[2]);
	}
	else {
		load_hostnames(argv[1], NULL, get_fqdn());
	}

	for (argi = 2; (argi < argc); argi++) {
		char s[1024];
		char *p;
		char *hname;
		char *hostip = NULL;

handlehost:
		hname = knownhost(argv[argi], &hostip, GH_IGNORE);
		if (hname == NULL) {
			printf("Unknown host '%s'\n", argv[argi]);
			continue;
		}
		if (strcmp(hname, argv[argi])) {
			printf("Using canonical name '%s'\n", hname);
		}
		h = hostinfo(hname);

		if (h == NULL) { printf("Host %s not found\n", argv[argi]); continue; }

		val = xmh_item_multi(h, XMH_PAGEPATH);	/* reset the static h record */
		val = xmh_item_walk(h);
		printf("Entry for host %s\n", h->hostname);
		while (val) {
			printf("\t%s\n", val);
			val = xmh_item_walk(NULL);
		}

		do {
			*s = '\0';
			printf("Pick item:"); fflush(stdout); if (!fgets(s, sizeof(s), stdin)) return 0;
			p = strchr(s, '\n'); if (p) *p = '\0';
			if (*s == '!') {
				load_hostnames(argv[1], NULL, get_fqdn());
				/* Must restart! The "h" handle is no longer valid. */
				goto handlehost;
			}
			else if (*s == '>') {
				val = xmh_item_multi(h, XMH_PAGEPATH);
				while (val) {
					printf("\t%s value is: '%s'\n", s, val);
					val = xmh_item_multi(NULL, XMH_PAGEPATH);
				}
			}
			else if (strncmp(s, "set ", 4) == 0) {
				xmh_set_item(h, XMH_DATA, strdup(s+4));
			}
			else if (*s) {
				val = xmh_item_byname(h, s);
				if (val) printf("\t%s value is: '%s'\n", s, val);
				else printf("\t%s not found\n", s);
			}
		} while (*s);
	}

	destroy_hosttree();
	return 0;
}

#endif

