/*----------------------------------------------------------------------------*/
/* Big Brother host finder.                                                   */
/*                                                                            */
/* This is a CGI script to find hosts in the BB webpages without knowing      */
/* their full name. When you have 1200+ hosts split on 60+ pages, it can be   */
/* tiresome to do a manual search to find a host ...                          */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-findhost.c,v 1.2 2003-12-10 20:58:11 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "bbgen.h"
#include "loadhosts.h"
#include "util.h"
#include "debug.h"

/* Global vars */
bbgen_page_t    *pagehead = NULL;                       /* Head of page list */
link_t          *linkhead = NULL;                       /* Head of links list */
hostlist_t      *hosthead = NULL;                       /* Head of hosts list */
summary_t       *sumhead = NULL;                        /* Summaries we send out */
int             fqdn = 1;                               /* BB FQDN setting */
time_t          reportstart = 0;
double          reportwarnlevel = 97.0;


/* The list of hosts we get from CGI */
char **hostlist;

void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	char *query;
	char *token;

	if (getenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request");
		return;
	}
	else query = urldecode("QUERY_STRING");

	if (!urlvalidate(query, NULL)) {
		errormsg("Invalid request");
		return;
	}

	token = strtok(query, "&");
	while (token) {
		if (argnmatch(token, "host=")) {
			int idx = 0;

			/* How many hosts ? Count the number of spaces = (number of hosts - 1) */
			for (token = strchr(query, ' '), idx=1; (token); token=strchr(token+1, ' '), idx++);
			/* And remember to add an extra for the final NULL */
			hostlist = (char **)malloc((idx+1)*sizeof(char *));

			token = strtok(query+5, " ");
			idx = 0;
			while (token) {
				hostlist[idx] = malcop(token);
				idx++;

				token = strtok(NULL, " ");
			}
			hostlist[idx] = NULL;
		}
		else token = strtok(NULL, "&");
	}

	free(query);
}


int main(int argc, char *argv[])
{
	char *pageset = NULL;
	hostlist_t *hostwalk;
	int i;

	printf("Content-Type: text/html\n\n");

	parse_query();

        /* It's ok with these hardcoded values, as they are not used for this page */
        sethostenv("", "", "", colorname(COL_BLUE));
        headfoot(stdout, "hostsvc", "", "header", COL_BLUE);

	pagehead = load_bbhosts(pageset);

	printf("<br><br><CENTER><TABLE SUMMARY=\"Hostlist\" WIDTH=60%%>\n");
	printf("<tr><th align=left width=20%%>Hostname</th><th align=left width=80%%>Location</th></tr>\n");

	for (i=0; (hostlist[i]); i++) {
		int gotany = 0;
		int match;

        	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {

        		if (strncasecmp(hostlist[i], hostwalk->hostentry->hostname, strlen(hostlist[i])) == 0) {
				/*
				 * We do a case-insensitive compare of only the letters in the given
				 * searchstring, assuming a trailing wildcard.
				 *
				 * This could be improved to do regex matching....
				 */
				match = 1;
			}
			else {
				match = 0;
			}


			if (match) {
				printf("<tr>\n");
				printf("<td align=left>%s</td>\n", hostwalk->hostentry->hostname);
				printf("<td align=left><a href=\"%s/%s#%s\">%s</a></td>\n",
                       			getenv("BBWEB"), 
					hostpage_link(hostwalk->hostentry), 
					hostwalk->hostentry->hostname,
					hostpage_name(hostwalk->hostentry));
				printf("</tr>\n");

				gotany++;
			}
		}

		if (!gotany) printf("<tr><td align=left>%s</td><td align=left>Not found</td></tr>\n", hostlist[i]);

	}
	printf("</TABLE></CENTER>\n");

        headfoot(stdout, "hostsvc", "", "footer", COL_BLUE);
	return 0;
}

