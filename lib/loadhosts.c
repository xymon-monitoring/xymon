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

static char rcsid[] = "$Id: loadhosts.c,v 1.6 2004-11-23 21:46:50 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "libbbgen.h"

#include "loadhosts.h"

typedef struct pagelist_t {
	char *pagename;
	struct pagelist_t *next;
} pagelist_t;

typedef struct namelist_t {
	char ip[16];
	char *bbhostname;	/* Name for item 2 of bb-hosts */
	char *clientname;	/* CLIENT: tag - host alias */
	char *displayname;	/* NAME: tag - display purpose only */
	char *downtime;
	struct pagelist_t *page;
	struct namelist_t *next;
} namelist_t;
static pagelist_t *pghead = NULL;
static namelist_t *namehead = NULL;

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

void load_hostnames(char *bbhostsfn, int fqdn)
{
	FILE *bbhosts;
	int ip1, ip2, ip3, ip4;
	char hostname[MAXMSG];
	char l[MAXMSG];
	pagelist_t *curtoppage, *curpage;

	while (namehead) {
		namelist_t *walk = namehead;

		namehead = namehead->next;

		if (walk->bbhostname == walk->clientname) {
			free(walk->bbhostname);
			walk->clientname = NULL;
		}
		if (walk->clientname) free(walk->clientname);
		if (walk->displayname) free(walk->displayname);
		if (walk->downtime) free(walk->downtime);
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
			char *startoftags, *tag, *p;
			char displayname[MAXMSG];
			char clientname[MAXMSG];
			char downtime[MAXMSG];

			namelist_t *newitem = malloc(sizeof(namelist_t));

			if (!fqdn) {
				/* Strip any domain from the hostname */
				char *p = strchr(hostname, '.');
				if (p) *p = '\0';
			}

			sprintf(newitem->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

			newitem->bbhostname = strdup(hostname);
			newitem->clientname = newitem->bbhostname;
			newitem->downtime = NULL;
			newitem->displayname = NULL;
			newitem->page = curpage;
			newitem->next = namehead;
			namehead = newitem;

			displayname[0] = clientname[0] = downtime[0] = '\0';
			startoftags = strchr(l, '#');
			if (startoftags == NULL) startoftags = ""; else startoftags++;

			tag = strtok(startoftags, " \t\r\n");
			while (tag) {
				if (strncmp(tag, "NAME:", strlen("NAME:")) == 0) {
                                        p = tag+strlen("NAME:");
                                        if (*p == '\"') {
                                                p++;
                                                strcpy(displayname, p);
                                                p = strchr(displayname, '\"');
                                                if (p) *p = '\0';
                                                else {
                                                        /* Scan forward to next " in input stream */
                                                        tag = strtok(NULL, "\"\r\n");
                                                        if (tag) {
                                                                strcat(displayname, " ");
                                                                strcat(displayname, tag);
                                                        }
                                                }
                                        }
                                        else {
                                                strcpy(displayname, p);
                                        }
				}
				else if (strncmp(tag, "CLIENT:", strlen("CLIENT:")) == 0) {
                                        p = tag+strlen("CLIENT:");
                                        strcpy(clientname, p);
				}
				else if (strncmp(tag, "DOWNTIME=", strlen("DOWNTIME=")) == 0) {
                                        strcpy(downtime, tag);
				}
				if (tag) tag = strtok(NULL, " \t\r\n");
			}

			if (strlen(displayname) > 0) newitem->displayname = strdup(displayname);
			if (strlen(clientname) > 0) newitem->clientname = strdup(clientname);
			if (strlen(downtime) > 0) newitem->downtime = strdup(downtime);
		}
	}
	stackfclose(bbhosts);
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

char *hostdispname(char *hostname)
{
	namelist_t *walk;

	for (walk = namehead; (walk && (strcmp(walk->bbhostname, hostname) != 0)); walk = walk->next);
	return ((walk && walk->displayname) ? walk->displayname : hostname);
}

char *hostpagename(char *hostname)
{
	namelist_t *walk;

	for (walk = namehead; (walk && (strcmp(walk->bbhostname, hostname) != 0)); walk = walk->next);
	return (walk ? walk->page->pagename : "");
}

