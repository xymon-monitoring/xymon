/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module for Hobbit, responsible for loading the bb-hosts  */
/* file and keeping track of what hosts are known, their aliases and planned  */
/* downtime settings etc.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/


static char rcsid[] = "$Id: loadhosts.c,v 1.27 2005-03-30 15:41:20 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "libbbgen.h"

static pagelist_t *pghead = NULL;
static namelist_t *namehead = NULL;
static const char *bbh_item_key[BBH_LAST];
static char *documentation_url = NULL;

static void bbh_item_list_setup(void)
{
	static int setupdone = 0;
	int i;

	if (setupdone) return;

	/* Doing it this way makes sure the index matches the value */
	setupdone = 1;
	memset(bbh_item_key, 0, sizeof(bbh_item_key));
	bbh_item_key[BBH_NET] = "NET:";
	bbh_item_key[BBH_DISPLAYNAME] = "NAME:";
	bbh_item_key[BBH_CLIENTALIAS] = "CLIENT:";
	bbh_item_key[BBH_COMMENT] = "COMMENT:";
	bbh_item_key[BBH_DESCRIPTION] = "DESCR:";
	bbh_item_key[BBH_NK] = "NK:";
	bbh_item_key[BBH_NKTIME] = "NKTIME=";
	bbh_item_key[BBH_LARRD] = "LARRD:";
	bbh_item_key[BBH_WML] = "WML:";
	bbh_item_key[BBH_NOPROP] = "NOPROP:";
	bbh_item_key[BBH_NOPROPRED] = "NOPROPRED:";
	bbh_item_key[BBH_NOPROPYELLOW] = "NOPROPYELLOW:";
	bbh_item_key[BBH_NOPROPPURPLE] = "NOPROPPURPLE:";
	bbh_item_key[BBH_NOPROPACK] = "NOPROPACK:";
	bbh_item_key[BBH_REPORTTIME] = "REPORTTIME=";
	bbh_item_key[BBH_WARNPCT] = "WARNPCT:";
	bbh_item_key[BBH_DOWNTIME] = "DOWNTIME=";
	bbh_item_key[BBH_SSLDAYS] = "ssldays=";
	bbh_item_key[BBH_DEPENDS] = "depends=";
	bbh_item_key[BBH_FLAG_NOINFO] = "noinfo";
	bbh_item_key[BBH_FLAG_NODISP] = "nodisp";
	bbh_item_key[BBH_FLAG_NOBB2] = "nobb2";
	bbh_item_key[BBH_FLAG_PREFER] = "prefer";
	bbh_item_key[BBH_FLAG_NOSSLCERT] = "nosslcert";
	bbh_item_key[BBH_FLAG_TRACE] = "trace";
	bbh_item_key[BBH_FLAG_NOTRACE] = "notrace";
	bbh_item_key[BBH_FLAG_NOCONN] = "noconn";
	bbh_item_key[BBH_FLAG_NOPING] = "noping";
	bbh_item_key[BBH_FLAG_DIALUP] = "dialup";
	bbh_item_key[BBH_FLAG_TESTIP] = "testip";
	bbh_item_key[BBH_FLAG_BBDISPLAY] = "BBDISPLAY";
	bbh_item_key[BBH_FLAG_BBNET] = "BBNET";
	bbh_item_key[BBH_FLAG_BBPAGER] = "BBPAGER";
	bbh_item_key[BBH_FLAG_LDAPFAILYELLOW] = "ldapyellowfail";
	bbh_item_key[BBH_LDAPLOGIN] = "ldaplogin=";

	i = 0; while (bbh_item_key[i]) i++;
	if (i != BBH_IP) {
		errprintf("ERROR: Setup failure in bbh_item_key position %d\n", i);
	}
}


static char *bbh_find_item(namelist_t *host, enum bbh_item_t item)
{
	int i;

	bbh_item_list_setup();
	i = 0;
	while (host->elems[i] && strncasecmp(host->elems[i], bbh_item_key[item], strlen(bbh_item_key[item]))) i++;
	return (host->elems[i] ? (host->elems[i] + strlen(bbh_item_key[item])) : NULL);
}

static void initialize_hostlist(char *docurl)
{
	if (documentation_url) xfree(documentation_url); documentation_url = NULL;

	while (namehead) {
		namelist_t *walk = namehead;

		namehead = namehead->next;

		if (walk->bbhostname) xfree(walk->bbhostname);
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

	if (docurl) documentation_url = strdup(docurl);

	/* Setup the top-level page */
	pghead = (pagelist_t *) malloc(sizeof(pagelist_t));
	pghead->pagepath = strdup("");
	pghead->pagetitle = strdup("");
	pghead->next = NULL;
}

#ifdef MYSQL
#include "loadhosts_mysql.c"
#else
#include "loadhosts_file.c"
#endif


char *knownhost(char *hostname, char *hostip, int ghosthandling, int *maybedown)
{
	/*
	 * ghosthandling = 0 : Default BB method (case-sensitive, no logging, keep ghosts)
	 * ghosthandling = 1 : Case-insensitive, no logging, drop ghosts
	 * ghosthandling = 2 : Case-insensitive, log ghosts, drop ghosts
	 */
	namelist_t *walk = NULL;
	static char *result = NULL;

	if (result == NULL) result = (char *)malloc(MAXMSG);

	/* Find the host */
	for (walk = namehead; (walk && (strcasecmp(walk->bbhostname, hostname) != 0) && (strcasecmp(walk->clientname, hostname) != 0)); walk = walk->next);
	if (walk) {
		/*
		 * Force our version of the hostname. Done here so CLIENT works always.
		 */
		strcpy(hostip, walk->ip);
		strcpy(result, walk->bbhostname);
		if (walk->downtime) *maybedown = within_sla(walk->downtime, 0);
	}
	else {
		strcpy(result, hostname);
		*maybedown = 0;
		*hostip = '\0';
	}

	/* If default method, just say yes */
	if (ghosthandling == 0) return result;

	/* Allow all summaries and modembanks */
	if (strcmp(hostname, "summary") == 0) return result;
	if (strcmp(hostname, "dialup") == 0) return result;

	return (walk ? result : NULL);
}

int knownloghost(char *logdir)
{
	namelist_t *walk = NULL;

	/* Find the host */
	for (walk = namehead; (walk && (strcasecmp(walk->logname, logdir) != 0)); walk = walk->next);

	return (walk != NULL);
}

namelist_t *hostinfo(char *hostname)
{
	namelist_t *walk;

	for (walk = namehead; (walk && (strcmp(walk->bbhostname, hostname) != 0)); walk = walk->next);
	return walk;
}

char *bbh_item(namelist_t *host, enum bbh_item_t item)
{
	static char *result;
	static char *inttxt = NULL;
	char *p;

	if (inttxt == NULL) inttxt = (char *)malloc(10);

	if (host == NULL) return NULL;

	switch (item) {
	  case BBH_CLIENTALIAS: 
		  return host->clientname;

	  case BBH_DOWNTIME:
		  return host->downtime;

	  case BBH_IP:
		  return host->ip;

	  case BBH_BANKSIZE:
		  sprintf(inttxt, "%d", host->banksize);
		  return inttxt;

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

	  case BBH_DOCURL:
		  if (documentation_url) {
			if (result) xfree(result);
			result = (char *)malloc(strlen(documentation_url) + strlen(host->bbhostname) + 1);
			sprintf(result, documentation_url, host->bbhostname);
		  	return result;
		  }
		  else
			return NULL;

	  default:
		  return bbh_find_item(host, item);
	}

	return NULL;
}

char *bbh_custom_item(namelist_t *host, char *key)
{
	int i;

	i = 0;
	while (host->elems[i] && strncmp(host->elems[i], key, strlen(key))) i++;

	return host->elems[i];
}

char *bbh_item_walk(namelist_t *host)
{
	static int idx = -1;
	static namelist_t *curhost = NULL;
	char *result;

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


#ifdef STANDALONE

int main(int argc, char *argv[])
{
	int argi;
	namelist_t *hosts, *h;
	char *val;

	hosts = load_hostnames(argv[1], NULL, 1, NULL);

	for (argi = 2; (argi < argc); argi++) {
		h = hostinfo(argv[argi]);

		if (h == NULL) { printf("Host %s not found\n", argv[argi]); continue; }

		val = bbh_item_walk(h);
		printf("Entry for host %s\n", h->bbhostname);
		while (val) {
			printf("\t%s\n", val);
			val = bbh_item_walk(NULL);
		}

		val = bbh_custom_item(h, "GMC:");
		if (val) printf("\tGMC value is: %s\n", val);

		val = bbh_item(h, BBH_NET);
		if (val) printf("\tBBH_NET is %s\n", val);

		val = bbh_item(h, BBH_PAGEPATH);
		if (val) printf("\tBBH_PAGEPATH is %s\n", val);
	}

	return 0;
}

#endif

