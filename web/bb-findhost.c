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
/* 2004/09/08 - Werner Michels [wm]                                           */
/*              Added support regular expression on the host search.          */
/*              Minor changes on errormsg() and error messagess.              */
/*		The parse_query was rewriten to meet the new needs.           */
/*                                                                            */
/*----------------------------------------------------------------------------*/

/*
 * [wm] - Functionality change
 *	Now the search is done using only Extended POSIX pattern match.
 *	If you don't know how Regex works, look at "man 7 regex".
 *	If you want search for multiple hosts use "name1|name2|name3" insted
 *	of separating them by spaces. You can now search for host (displayname)
 *	with spaces.
 *	Emtpy search string will list all the hosts.
 *
 *
 *
 * [wm] - TODO
 *	- Move the new global vars to local vars and use function parameters
 *	- Verify the security implication of removing the urlvalidate() call
 *	- Move to POST instead of GET to receive the FORM data
 *	- Add the posibility to choose where to search (hostname, description
 *	  host comment, host displayname, host clientname...)
 *
 */

static char rcsid[] = "$Id: bb-findhost.c,v 1.6 2004-10-30 15:36:08 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/*[wm] For the POSIX regex support*/
#include <sys/types.h> 
#include <regex.h> 



#include "bbgen.h"
#include "util.h"
#include "loadhosts.h"

/* Global vars */
bbgen_page_t    *pagehead = NULL;                       /* Head of page list */
summary_t       *sumhead = NULL;                        /* Summaries we send out */
time_t          reportstart = 0;
double          reportwarnlevel = 97.0;
int             fqdn = 1;                               /* BB FQDN setting */

/*
 * [wm] To support regex searching
 */
char	*pSearchPat = NULL;			/* What're searching for (now its regex, not a hostlist) */
int 	re_flag     = REG_EXTENDED|REG_NOSUB|REG_ICASE; /* default regcomp flags see man 3 regcomp 	*/
							/* You must remove REG_ICASE for case sensitive */


void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>BigBrother (bbgen) FindHost Error</title></head>\n");
	printf("<body><BR><BR><BR>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	char *query;
	char *token;

	if (getenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request: QUERY_STRING is NULL/Empty!");
		return;
	}
	else query = urldecode("QUERY_STRING");

	token = strtok(query, "&");
	while (token) {
		char *pEqual;	/* points to equal sign */
		char *pVarName; /* Points to the var (var=value) start */
		char *pValue;	/* Points to the value start */

		if ( (pEqual = strchr(token, '=')) != NULL ) {
			*pEqual++ = '\0';
			pValue   = pEqual;
			pVarName = token; 
			
			if ( strcmp (pVarName, "host") == 0 ) {

				/* 
				 * [wm] maybe we should use strndup
				 *      or use malloc and strncpy to be safer 
				 */
				if (  (pSearchPat = (char *)strdup (pValue)) == NULL ){
					errormsg("Insufficient memory to allocate search pattern");
					return; 	/* never comes here than errormsg does exit */
				}	

			} else if ( strcmp (pVarName, "case_sensitive") == 0 ) {
				/* remove the ignore case flag */

				re_flag ^= REG_ICASE;

			} else {
				if ( 0 )  /* set this to 1 if you want debug info */
					fprintf (stderr, "bb-findhost.cgi: Ignoring CGI Variable: %s\n", pVarName);
			}
			
		}

		/* get next token */
		token = strtok(NULL, "&");
	}

	free(query);
}



int main(int argc, char *argv[])
{
	char *pageset = NULL;
	hostlist_t *hostwalk, *clonewalk;
	int i;

	int gotany = 0;


	/*[wm] regex support */
	#define BUFSIZE		256
	regex_t re;
	char    re_errstr[BUFSIZE];
	int 	re_status;
	host_t	*he;					/* HostEntry pointer (dereferencing)... :)	*/


	parse_query();

	setvbuf(stdout, NULL, _IONBF, 0);   		/* [wm] unbuffer stdout */
	printf("Content-Type: text/html\n\n");

        /* It's ok with these hardcoded values, as they are not used for this page */
        sethostenv("", "", "", colorname(COL_BLUE));
        headfoot(stdout, "hostsvc", "", "header", COL_BLUE);

	pagehead = load_bbhosts(pageset);

	printf("<br><br><CENTER><TABLE CELLPADDING=5 SUMMARY=\"Hostlist\">\n");
	printf("<tr><th align=left>Hostname (DisplayName)</th><th align=left>Location (Group Name)</th></tr>\n");


	if ( (re_status = regcomp(&re, pSearchPat, re_flag)) != 0 ) {
		regerror(re_status, &re, re_errstr, BUFSIZE);

		printf("<tr><td align=left><font color=red>%s</font></td>\n",  pSearchPat);
		printf("<td align=left><font color=red>%s</font></td></tr>\n", re_errstr);
	} else {

	       	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
			he = hostwalk->hostentry; 
			
			/* 
			 * [wm] - Allow the search to be done on the hostname
			 * 	also on the "displayname" and the host comment
			 *	Maybe this should be implemented by changing the HTML form, but until than..
			 * we're supposing that he->hostname will NEVER be null	
			 */
	       		if ( regexec (&re, he->hostname, (size_t)0, NULL, 0) == 0  ||
	       			(he->displayname && regexec (&re, he->displayname, (size_t)0, NULL, 0) == 0) ||
				(he->comment     && regexec (&re, he->comment, 	   (size_t)0, NULL, 0) == 0)   ) {
	
				/*  match */
				printf("<tr>\n");
				printf("<td align=left> %s </td>\n", he->displayname ? he->displayname : he->hostname);
				printf("<td align=left> <a href=\"%s/%s#%s\">%s</a>\n",
	                     		getenv("BBWEB"), 
					hostpage_link(he), 
					he->hostname,
					hostpage_name(he));

				for (clonewalk = hostwalk->clones; (clonewalk); clonewalk = clonewalk->next) {
					printf("<br><a href=\"%s/%s#%s\">%s</a>\n",
						getenv("BBWEB"), 
						hostpage_link(clonewalk->hostentry), 
						clonewalk->hostentry->hostname,
						hostpage_name(clonewalk->hostentry));
				}

				printf("</td>\n</tr>\n");
	
				gotany++;
			}
		}

		regfree (&re); 	/*[wm] - free regex compiled patern */
	
		if (!gotany) printf("<tr><td align=left>%s</td><td align=left>Not found</td></tr>\n", pSearchPat);
	} 


	printf("</TABLE></CENTER>\n");

        headfoot(stdout, "hostsvc", "", "footer", COL_BLUE);

	/* [wm] - Free the strdup allocated memory */
	if (pSearchPat) free (pSearchPat);

	return 0;
}

