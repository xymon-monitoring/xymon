#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "bbgen.h"
#include "util.h"
#include "loadhosts.h"

typedef struct namelist_t {
	char ip[16];
	char *bbhostname;	/* Name for item 2 of bb-hosts */
	char *clientname;	/* CLIENT: tag - host alias */
	char *displayname;	/* NAME: tag - display purpose only */
	char *downtime;
	struct namelist_t *next;
} namelist_t;
static namelist_t *namehead = NULL;


void load_hostnames(char *bbhostsfn, int fqdn)
{
	FILE *bbhosts;
	int ip1, ip2, ip3, ip4;
	char hostname[MAXMSG];
	char l[MAXMSG];

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

	bbhosts = stackfopen(bbhostsfn, "r");
	while (stackfgets(l, sizeof(l), "include", NULL)) {
		if (sscanf(l, "%d.%d.%d.%d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
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


char *knownhost(char *hostname, char *srcip, int ghosthandling, int *maybedown)
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

	/* If default method, just say yes */
	if (ghosthandling == 0) return result;

	/* Allow all summaries and modembanks */
	if (strcmp(hostname, "summary") == 0) return result;
	if (strcmp(hostname, "dialup") == 0) return result;

	/* See if we know this hostname */
	for (walk = namehead; (walk && (strcasecmp(walk->bbhostname, hostname) != 0) && (strcasecmp(walk->clientname, hostname) != 0)); walk = walk->next);
	if (walk) {
		/*
		 * Force our version of the hostname
		 */
		strcpy(result, walk->bbhostname);
		if (walk->downtime) *maybedown = within_sla(walk->downtime, "DOWNTIME", 0);
	}
	else {
		/* Log a ghost. */
		if (ghosthandling >= 2) {
			errprintf("Caught a ghost '%s' (IP:%s)\n", hostname, srcip);
		}
	}

	return (walk ? result : NULL);
}

char *hostdispname(char *hostname)
{
	namelist_t *walk;

	for (walk = namehead; (walk && (strcmp(walk->bbhostname, hostname) != 0)); walk = walk->next);
	return ((walk && walk->displayname) ? walk->displayname : hostname);
}

