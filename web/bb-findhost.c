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

static char rcsid[] = "$Id: bb-findhost.c,v 1.17 2005-02-16 13:53:34 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <fcntl.h>

/*[wm] For the POSIX regex support*/
#include <regex.h> 

#include "libbbgen.h"

/* Global vars */

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

	if (xgetenv("QUERY_STRING") == NULL) {
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

	xfree(query);
}



int main(int argc, char *argv[])
{
	namelist_t *hosthead, *hostwalk, *clonewalk;
	int argi;

	int gotany = 0;


	/*[wm] regex support */
	#define BUFSIZE		256
	regex_t re;
	char    re_errstr[BUFSIZE];
	int 	re_status;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
	}

	if ((xgetenv("QUERY_STRING") == NULL) || (strlen(xgetenv("QUERY_STRING")) == 0)) {
		/* Present the query form */
		int formfile;
		char formfn[PATH_MAX];

		sprintf(formfn, "%s/web/findhost_form", xgetenv("BBHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1);
			read(formfile, inbuf, st.st_size);
			inbuf[st.st_size] = '\0';
			close(formfile);

			printf("Content-Type: text/html\n\n");
			sethostenv("", "", "", colorname(COL_BLUE));

			headfoot(stdout, "findhost", "", "header", COL_BLUE);
			output_parsed(stdout, inbuf, COL_BLUE, "findhost");
			headfoot(stdout, "findhost", "", "footer", COL_BLUE);

			xfree(inbuf);
		}
		return 0;
	}

	parse_query();

	setvbuf(stdout, NULL, _IONBF, 0);   		/* [wm] unbuffer stdout */
	printf("Content-Type: text/html\n\n");

        /* It's ok with these hardcoded values, as they are not used for this page */
        sethostenv("", "", "", colorname(COL_BLUE));
        headfoot(stdout, "findhost", "", "header", COL_BLUE);

	hosthead = load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn(), NULL);

	printf("<br><br><CENTER><TABLE CELLPADDING=5 SUMMARY=\"Hostlist\">\n");
	printf("<tr><th align=left>Hostname (DisplayName)</th><th align=left>Location (Group Name)</th></tr>\n");


	if ( (re_status = regcomp(&re, pSearchPat, re_flag)) != 0 ) {
		regerror(re_status, &re, re_errstr, BUFSIZE);

		printf("<tr><td align=left><font color=red>%s</font></td>\n",  pSearchPat);
		printf("<td align=left><font color=red>%s</font></td></tr>\n", re_errstr);
	} else {

	       	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
			/* 
			 * [wm] - Allow the search to be done on the hostname
			 * 	also on the "displayname" and the host comment
			 *	Maybe this should be implemented by changing the HTML form, but until than..
			 * we're supposing that hostname will NEVER be null	
			 */
			char *hostname, *displayname, *comment, *ip;

			hostname = bbh_item(hostwalk, BBH_HOSTNAME);
			displayname = bbh_item(hostwalk, BBH_DISPLAYNAME);
			comment = bbh_item(hostwalk, BBH_COMMENT);
			ip = bbh_item(hostwalk, BBH_IP);

	       		if ( regexec (&re, hostname, (size_t)0, NULL, 0) == 0  ||
				(regexec(&re, ip, (size_t)0, NULL, 0) == 0)    ||
	       			(displayname && regexec (&re, displayname, (size_t)0, NULL, 0) == 0) ||
				(comment     && regexec (&re, comment, 	   (size_t)0, NULL, 0) == 0)   ) {
	
				/*  match */
				printf("<tr>\n");
				printf("<td align=left> %s </td>\n", displayname ? displayname : hostname);
				printf("<td align=left> <a href=\"%s/%s#%s\">%s</a>\n",
	                     		xgetenv("BBWEB"), 
					bbh_item(hostwalk, BBH_PAGEPATH),
					hostname,
					bbh_item(hostwalk, BBH_PAGEPATHTITLE));

				clonewalk = hostwalk->next;
				while (clonewalk && (strcmp(hostwalk->bbhostname, clonewalk->bbhostname) == 0)) {
					printf("<br><a href=\"%s/%s#%s\">%s</a>\n",
						xgetenv("BBWEB"), 
						bbh_item(clonewalk, BBH_PAGEPATH),
						clonewalk->bbhostname,
						bbh_item(clonewalk, BBH_PAGEPATHTITLE));
					clonewalk = clonewalk->next;
				}

				printf("</td>\n</tr>\n");
	
				gotany++;
			}
		}

		regfree (&re); 	/*[wm] - free regex compiled patern */
	
		if (!gotany) printf("<tr><td align=left>%s</td><td align=left>Not found</td></tr>\n", pSearchPat);
	} 


	printf("</TABLE></CENTER>\n");

        headfoot(stdout, "findhost", "", "footer", COL_BLUE);

	/* [wm] - Free the strdup allocated memory */
	if (pSearchPat) xfree(pSearchPat);

	return 0;
}

