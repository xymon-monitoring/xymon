/*----------------------------------------------------------------------------*/
/* Big Brother bb-hosts file grep'er                                          */
/*                                                                            */
/* This tool will pick out the hosts from a bb-hosts file that has one of the */
/* tags given on the command line. This allows an extension script to deal    */
/* with only the relevant parts of the bb-hosts file, instead of having to    */
/* parse the entire file.                                                     */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbhostgrep.c,v 1.15 2004-10-29 10:21:57 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

int main(int argc, char *argv[])
{ 
	FILE *bbhosts;
	char l[MAX_LINE_LEN];
	char *netstring = NULL;
	char *p;
	int extras = 1;
	int testuntagged = 0;
	int argi;
	char *include2 = NULL;
	char **lookv;
	int lookc;

	if ((argc <= 1) || (strcmp(argv[1], "--help") == 0)) {
		printf("Usage:\n%s test1 [test1] [test2] ... \n", argv[0]);
		exit(1);
	}

	lookv = (char **)malloc(argc*sizeof(char *));
	lookc = 0;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--noextras") == 0) {
			extras = 0;
		}
		else if (strcmp(argv[argi], "--test-untagged") == 0) {
			testuntagged = 1;
		}
		else if (strcmp(argv[argi], "--version") == 0) {
			printf("bbhostgrep version %s\n", VERSION);
			exit(0);
		}
		else if (strcmp(argv[argi], "--bbnet") == 0) {
			include2 = "netinclude";
		}
		else if (strcmp(argv[argi], "--bbdisp") == 0) {
			include2 = "dispinclude";
		}
		else {
			lookv[lookc] = strdup(argv[argi]);
			lookc++;
		}
	}

	if (getenv("BBHOSTS") == NULL) {
		errprintf("Environment variable BBHOSTS is not set - aborting\n");
		exit(2);
	}

	/* Each network test tagged with NET:locationname */
	p = getenv("BBLOCATION");
	if (p) {
		netstring = (char *) malloc(strlen(p)+5);
		sprintf(netstring, "NET:%s", p);
	}


	bbhosts = stackfopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		errprintf("Cannot open the BBHOSTS file '%s'\n", getenv("BBHOSTS"));
		exit(1);
	}

	while (stackfgets(l, sizeof(l), "include", include2)) {
		int ip1, ip2, ip3, ip4;
		char hostname[MAX_LINE_LEN];
		char wantedtags[MAX_LINE_LEN];
		int wanted = 0;
		int sla=-1;
		char *startoftags = strchr(l, '#');

		p = strchr(l, '\n');
		if (p) {
			*p = '\0';
		}
		else {
			errprintf("Warning: Lines in bb-hosts too long or has no newline: '%s'\n", l);
		}

		/*
		 * We don't need to care about entries without a "#" mark in them, as
		 * we are looking for hosts that have at least one tag.
		 */
		wantedtags[0] = '\0';
		if ( startoftags && 
		     (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) &&
		     ( 
		       (netstring == NULL) || 
		       (strstr(l, netstring) != NULL) || 
		       (testuntagged && (strstr(l, "NET:") == NULL)) 
		     ) 
		   ) {
			char *item;
			char *realitem;

			if (*startoftags == '#') startoftags++;
			while ((*startoftags != '\0') && isspace((int) *startoftags)) startoftags++;

			realitem = item = strtok(startoftags, " \t\r\n");
			while (item) {
				if ((*item == '!') || (*item == '~') || (*item == '?')) realitem++;

				if (extras && (strcasecmp(realitem, "dialup") == 0)) strcat(wantedtags, " dialup");
				else if (extras && (strcasecmp(realitem, "testip") == 0)) strcat(wantedtags, " testip");
				else if (extras && (strncasecmp(realitem, "SLA=", 4) == 0)) sla = within_sla(l, "SLA", 1);
				else if (extras && (strncasecmp(realitem, "DOWNTIME=", 9) == 0)) sla = !within_sla(l, "DOWNTIME", 0);
				else {
					int i;

					for (i=0; (i<lookc); i++) {
						if (lookv[i][strlen(lookv[i])-1] == '*') {
							if (strncasecmp(realitem, lookv[i], strlen(lookv[i])-1) == 0) {
								strcat(wantedtags, " ");
								strcat(wantedtags, (extras ? item : realitem));
								wanted = 1;
							}
						}
						else if (strcasecmp(realitem, lookv[i]) == 0) {
							strcat(wantedtags, " ");
							strcat(wantedtags, (extras ? item : realitem));
							wanted = 1;
						}
					}
				}

				realitem = item = strtok(NULL, " \t\r\n");
			}
		}

		if (wanted) {
			printf("%d.%d.%d.%d %s #%s", ip1, ip2, ip3, ip4, hostname, wantedtags);
			switch (sla) {
			  case -1: printf("\n"); break;
			  case  0: printf(" OUTSIDESLA\n"); break;
			  case  1: printf(" INSIDESLA\n"); break;
			}
		}
	}

	stackfclose(bbhosts);
	return 0;
}

