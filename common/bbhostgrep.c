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
	char bbhostsfn[MAX_PATH];
	FILE *bbhosts;
	char l[MAX_LINE_LEN];
	char *netstring = NULL;
	char *p;

	if (argc <= 1) {
		printf("Usage:\n%s test1 [test1] [test2] ... \n", argv[0]);
		exit (1);
	}

	if (getenv("BBHOSTS") == NULL) {
		errprintf("Environment variable BBHOSTS is not set - aborting\n");
		exit(2);
	}

	/* Each network test tagged with NET:locationname */
	p = getenv("BBLOCATION");
	if (p) {
		netstring = malloc(strlen(p)+5);
		sprintf(netstring, "NET:%s", p);
	}


	bbhosts = stackfopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		errprintf("Cannot open the BBHOSTS file '%s'\n", getenv("BBHOSTS"));
		exit(1);
	}

	while (stackfgets(l, sizeof(l), "include")) {
		int ip1, ip2, ip3, ip4;
		char hostname[MAX_LINE_LEN];
		char wantedtags[MAX_LINE_LEN];
		int wanted = 0;
		int sla=-1;
		char *startoftags = strchr(l, '#');

		/*
		 * We don't need to care about entries without a "#" mark in them, as
		 * we are looking for hosts that have at least one tag.
		 */
		wantedtags[0] = '\0';
		if ( startoftags && 
		     (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) &&
		     ((netstring == NULL) || (strstr(l, netstring) != NULL)) ) {
			char *item;
			char *realitem;

			if (*startoftags == '#') startoftags++;
			while ((*startoftags != '\0') && isspace((int) *startoftags)) startoftags++;

			realitem = item = strtok(startoftags, " \t");
			while (item) {
				if ((*item == '!') || (*item == '~') || (*item == '?')) realitem++;

				if (strcasecmp(realitem, "dialup") == 0) strcat(wantedtags, " dialup");
				else if (strcasecmp(realitem, "testip") == 0) strcat(wantedtags, " testip");
				else if (strncasecmp(realitem, "SLA=", 4) == 0) sla = within_sla(l);
				else {
					int i;

					for (i=1; (i<argc); i++) {
						if (strcasecmp(realitem, argv[i]) == 0) {
							strcat(wantedtags, " ");
							strcat(wantedtags, item);
							wanted = 1;
						}
					}
				}

				realitem = item = strtok(NULL, " \t");
			}
		}

		if (wanted) {
			printf("%d.%d.%d.%d %s #%s", ip1, ip2, ip3, ip4, hostname, wantedtags);
			switch (sla) {
			  case -1: printf("\n"); break;
			  case  0: printf("OUTSIDESLA\n"); break;
			  case  1: printf("INSIDESLA\n"); break;
			}
		}
	}

	stackfclose(bbhosts);
	return 0;
}

