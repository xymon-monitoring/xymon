/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This is a library module for bbgend, responsible for loading the bb-hosts  */
/* file and keeping track of what hosts are known, their aliases and planned  */
/* downtime settings etc.                                                     */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: loadhosts.c,v 1.11 2004-12-13 23:07:49 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "libbbgen.h"

#include "loadhosts.h"

static pagelist_t *pghead = NULL;
static namelist_t *namehead = NULL;

static const char *bbh_item_key[BBH_LAST];

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
	bbh_item_key[BBH_NOPROPRED] = "NOPROPRED:";
	bbh_item_key[BBH_NOPROPYELLOW] = "NOPROPYELLOW:";
	bbh_item_key[BBH_NOPROPPURPLE] = "NOPROPPURPLE:";
	bbh_item_key[BBH_NOPROPACK] = "NOPROPACK:";
	bbh_item_key[BBH_REPORTTIME] = "REPORTTIME=";
	bbh_item_key[BBH_WARNPCT] = "WARNPCT:";
	bbh_item_key[BBH_DOWNTIME] = "DOWNTIME=";
	bbh_item_key[BBH_SSLDAYS] = "ssldays=";
	bbh_item_key[BBH_DEPENDS] = "depends=";
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

	i = 0; while (bbh_item_key[i]) i++;
	if (i != BBH_RAW) {
		errprintf("ERROR: Setup failure in bbh_item_key position %d\n", i);
	}
}


static int pagematch(pagelist_t *pg, char *name)
{
	char *p = strrchr(pg->pagename, '/');

	if (p) {
		return (strcmp(p+1, name) == 0);
	}
	else {
		return (strcmp(pg->pagename, name) == 0);
	}
}

static char *bbh_find_item(namelist_t *host, enum bbh_item_t item)
{
	int i;

	bbh_item_list_setup();
	i = 0;
	while (host->elems[i] && strncmp(host->elems[i], bbh_item_key[item], strlen(bbh_item_key[item]))) i++;
	return (host->elems[i] ? (host->elems[i] + strlen(bbh_item_key[item])) : NULL);
}

namelist_t *load_hostnames(char *bbhostsfn, int fqdn)
{
	FILE *bbhosts;
	int ip1, ip2, ip3, ip4;
	char hostname[4096];
	char l[4096];
	pagelist_t *curtoppage, *curpage;

	while (namehead) {
		namelist_t *walk = namehead;

		namehead = namehead->next;

		free(walk->bbhostname);
		free(walk->rawentry);
		free(walk->allelems);
		free(walk->elems);
		free(walk);
	}

	while (pghead) {
		pagelist_t *walk = pghead;

		pghead = pghead->next;
		if (walk->pagename) free(walk->pagename);
		free(walk);
	}

	/* Setup the top-level page */
	pghead = (pagelist_t *) malloc(sizeof(pagelist_t));
	pghead->pagename = strdup("");
	pghead->next = NULL;
	curpage = curtoppage = pghead;

	bbhosts = stackfopen(bbhostsfn, "r");
	while (stackfgets(l, sizeof(l), "include", NULL)) {
		if (strncmp(l, "page ", 5) == 0) {
			pagelist_t *newp;
			char *tok;

			tok = strtok(l+5, " \n\t\r");
			if (tok) {
				newp = (pagelist_t *)malloc(sizeof(pagelist_t));
				newp->pagename = strdup(tok);
				newp->next = pghead;
				curtoppage = pghead = newp;
				curpage = newp;
			}
		}
		else if (strncmp(l, "subpage ", 8) == 0) {
			pagelist_t *newp;
			char *tok;

			tok = strtok(l+8, " \n\t\r");
			if (tok) {
				newp = (pagelist_t *)malloc(sizeof(pagelist_t));
				newp->pagename = malloc(strlen(curtoppage->pagename) + strlen(tok) + 2);
				sprintf(newp->pagename, "%s/%s", curtoppage->pagename, tok);
				newp->next = pghead;
				pghead = newp;
				curpage = newp;
			}
		}
		else if (strncmp(l, "subparent ", 10) == 0) {
			pagelist_t *newp, *parent;
			char *partok, *tok;

			parent = NULL; tok = NULL;

			partok = strtok(l+10, " \n\t\r");
			if (partok) {
				tok = strtok(NULL, " \n\t\r");
				for (parent = pghead; (parent && !pagematch(parent, partok)); parent = parent->next);
			}
			if (parent) {
				newp = (pagelist_t *)malloc(sizeof(pagelist_t));
				newp->pagename = malloc(strlen(parent->pagename) + strlen(tok) + 2);
				sprintf(newp->pagename, "%s/%s", parent->pagename, tok);
				newp->next = pghead;
				pghead = newp;
				curpage = newp;
			}
		}
		else if (sscanf(l, "%d.%d.%d.%d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			char *startoftags, *tag, *delim;
			int elemidx, elemsize;
			char clientname[4096];
			char downtime[4096];

			namelist_t *newitem = malloc(sizeof(namelist_t));
			namelist_t *iwalk;

			if (!fqdn) {
				/* Strip any domain from the hostname */
				char *p = strchr(hostname, '.');
				if (p) *p = '\0';
			}

			sprintf(newitem->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

			newitem->bbhostname = strdup(hostname);
			if (ip1 || ip2 || ip3 || ip4) newitem->preference = 1; else newitem->preference = 0;
			newitem->clientname = newitem->bbhostname;
			newitem->rawentry = NULL;
			newitem->downtime = NULL;
			newitem->page = curpage;
			newitem->data = NULL;

			clientname[0] = downtime[0] = '\0';
			startoftags = strchr(l, '#');
			if (startoftags == NULL) startoftags = ""; else startoftags++;
			startoftags += strspn(startoftags, " \t\r\n");
			newitem->rawentry = strdup(startoftags);
			newitem->allelems = strdup(startoftags);
			elemsize = 5;
			newitem->elems = (char **)malloc((elemsize+1)*sizeof(char *));

			tag = newitem->allelems; elemidx = 0;
			while (tag && *tag) {
				if (elemidx == elemsize) {
					elemsize += 5;
					newitem->elems = (char **)realloc(newitem->elems, (elemsize+1)*sizeof(char *));
				}
				newitem->elems[elemidx] = tag;

				/* Skip until we hit a whitespace or a quote */
				tag += strcspn(tag, " \t\r\n\"");
				if (*tag == '"') {
					delim = tag;

					/* Hit a quote - skip until the next matching quote */
					tag = strchr(tag+1, '"');
					if (tag != NULL) { 
						/* Found end-quote, NULL the item here and move on */
						*tag = '\0'; tag++; 
					}

					/* Now move quoted data one byte down (including the NUL) to kill quotechar */
					memmove(delim, delim+1, strlen(delim));
				}
				else {
					/* Normal end of item, NULL it and move on */
					*tag = '\0'; tag++;
				}

				/* 
				 * If we find a "noconn", drop preference value to 0.
				 * If we find a "prefer", up reference value to 2.
				 */
				if ((newitem->preference == 1) && (strcmp(newitem->elems[elemidx], "noconn") == 0))
					newitem->preference = 0;
				else if (strcmp(newitem->elems[elemidx], "prefer") == 0)
					newitem->preference = 2;

				/* Skip whitespace until start of next tag */
				if (tag) tag += strspn(tag, " \t\r\n");
				elemidx++;
			}

			newitem->elems[elemidx] = NULL;
			newitem->clientname = bbh_find_item(newitem, BBH_CLIENTALIAS);
			if (newitem->clientname == NULL) newitem->clientname = newitem->bbhostname;
			newitem->downtime = bbh_find_item(newitem, BBH_DOWNTIME);

			/* See if this host is defined before */
			for (iwalk = namehead; (iwalk && strcmp(iwalk->bbhostname, newitem->bbhostname)); iwalk = iwalk->next) ;
			if ((iwalk == NULL) || (newitem->preference > iwalk->preference)) {
				/* New item, or one with higher preference, so add to beginning of list */
				newitem->next = namehead;
				namehead = newitem;
			}
			else {
				/* Add after the existing (more preferred) entry */
				newitem->next = iwalk->next;
				iwalk->next = newitem;
			}
		}
	}
	stackfclose(bbhosts);

	return namehead;
}


char *knownhost(char *hostname, char *hostip, int ghosthandling, int *maybedown)
{
	/*
	 * ghosthandling = 0 : Default BB method (case-sensitive, no logging, keep ghosts)
	 * ghosthandling = 1 : Case-insensitive, no logging, drop ghosts
	 * ghosthandling = 2 : Case-insensitive, log ghosts, drop ghosts
	 */
	namelist_t *walk = NULL;
	static char result[MAXMSG];

	strcpy(result, hostname);
	*maybedown = 0;
	*hostip = '\0';

	/* Find the host */
	for (walk = namehead; (walk && (strcasecmp(walk->bbhostname, hostname) != 0) && (strcasecmp(walk->clientname, hostname) != 0)); walk = walk->next);
	if (walk) strcpy(hostip, walk->ip);

	/* If default method, just say yes */
	if (ghosthandling == 0) return result;

	/* Allow all summaries and modembanks */
	if (strcmp(hostname, "summary") == 0) return result;
	if (strcmp(hostname, "dialup") == 0) return result;

	/* See if we know this hostname */
	if (walk) {
		/*
		 * Force our version of the hostname
		 */
		strcpy(result, walk->bbhostname);
		if (walk->downtime) *maybedown = within_sla(walk->downtime, "DOWNTIME", 0);
	}

	return (walk ? result : NULL);
}

namelist_t *hostinfo(char *hostname)
{
	namelist_t *walk;

	for (walk = namehead; (walk && (strcmp(walk->bbhostname, hostname) != 0)); walk = walk->next);
	return walk;
}

char *bbh_item(namelist_t *host, enum bbh_item_t item)
{
	switch (item) {
	  case BBH_CLIENTALIAS: 
		  return host->clientname;

	  case BBH_DOWNTIME:
		  return host->downtime;

	  case BBH_RAW:
		  return host->rawentry;

	  case BBH_IP:
		  return host->ip;

	  case BBH_HOSTNAME: 
		  return host->bbhostname;

	  case BBH_PAGENAME:
		  return host->page->pagename;

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

	hosts = load_hostnames(argv[1], 1);

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
	}

	return 0;
}

#endif

