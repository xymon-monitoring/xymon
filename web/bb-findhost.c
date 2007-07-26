/*----------------------------------------------------------------------------*/
/* Hobbit host finder.                                                        */
/*                                                                            */
/* This is a CGI script to find hosts in the Hobbit webpages without knowing  */
/* their full name. When you have 1200+ hosts split on 60+ pages, it can be   */
/* tiresome to do a manual search to find a host ...                          */
/*                                                                            */
/* Copyright (C) 2003-2006 Henrik Storner <henrik@storner.dk>                 */
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

static char rcsid[] = "$Id: bb-findhost.c,v 1.36 2007-07-26 21:17:06 henrik Exp $";

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
cgidata_t *cgidata = NULL;
int	dojump     = 0;				/* If set and there is only one page, go directly to it */

void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Hobbit FindHost Error</title></head>\n");
	printf("<body><BR><BR><BR>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "host") == 0) {
			pSearchPat = (char *)strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "case_sensitive") == 0 ) {
			/* remove the ignore case flag */
			re_flag ^= REG_ICASE;
		}
		else if (strcasecmp(cwalk->name, "jump") == 0 ) {
			dojump = 1;
		}

		cwalk = cwalk->next;
	}
}


void print_header(void)
{
        /* It's ok with these hardcoded values, as they are not used for this page */
        sethostenv("", "", "", colorname(COL_BLUE), NULL);
	printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
        headfoot(stdout, "findhost", "", "header", COL_BLUE);
	printf("<br><br><CENTER><TABLE CELLPADDING=5 SUMMARY=\"Hostlist\">\n");
	printf("<tr><th align=left>Hostname (DisplayName)</th><th align=left>Location (Group Name)</th></tr>\n");
}

void print_footer(void)
{
	printf("</TABLE></CENTER>\n");
        headfoot(stdout, "findhost", "", "footer", COL_BLUE);
}


int main(int argc, char *argv[])
{
	void *hostwalk, *clonewalk;
	int argi;
	char *envarea = NULL;

	strbuffer_t *outbuf;
	char msgline[4096];
	char oneurl[10240];
	int gotany = 0;
	enum { OP_INITIAL, OP_YES, OP_NO } gotonepage = OP_INITIAL; /* Tracks if all matches are on one page */
	char *onepage = NULL;	/* If gotonepage==OP_YES, then this is the page */

	/*[wm] regex support */
	#define BUFSIZE		256
	regex_t re;
	char    re_errstr[BUFSIZE];
	int 	re_status;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
	}

	redirect_cgilog("bb-findhost");

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, "findhost", "findhost_form", COL_BLUE, getcurrenttime(NULL), NULL, NULL);
		return 0;
	}

	parse_query();

	if ( (re_status = regcomp(&re, pSearchPat, re_flag)) != 0 ) {
		regerror(re_status, &re, re_errstr, BUFSIZE);

		print_header();
		printf("<tr><td align=left><font color=red>%s</font></td>\n",  pSearchPat);
		printf("<td align=left><font color=red>%s</font></td></tr>\n", re_errstr);
		print_footer();

		return 0;
	}

	outbuf = newstrbuffer(0);
	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
	hostwalk = first_host();
	while (hostwalk) {
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
			addtobuffer(outbuf, "<tr>\n");
			sprintf(msgline, "<td align=left> %s </td>\n", displayname ? displayname : hostname);
			addtobuffer(outbuf, msgline);
			sprintf(oneurl, "%s/%s/#%s",
				xgetenv("BBWEB"), bbh_item(hostwalk, BBH_PAGEPATH), hostname);
			sprintf(msgline, "<td align=left> <a href=\"%s\">%s</a>\n",
				oneurl, bbh_item(hostwalk, BBH_PAGEPATHTITLE));
			addtobuffer(outbuf, msgline);
			gotany++;

			/* See if all of the matches so far are on one page */
			switch (gotonepage) {
			  case OP_INITIAL:
				gotonepage = OP_YES;
				onepage = bbh_item(hostwalk, BBH_PAGEPATH);
				break;

			  case OP_YES:
				if (strcmp(onepage, bbh_item(hostwalk, BBH_PAGEPATH)) != 0) gotonepage = OP_NO;
				break;

			  case OP_NO:
				break;
			}

			clonewalk = next_host(hostwalk, 1);
			while (clonewalk && (strcmp(bbh_item(hostwalk, BBH_HOSTNAME), bbh_item(clonewalk, BBH_HOSTNAME)) == 0)) {
				sprintf(msgline, "<br><a href=\"%s/%s/#%s\">%s</a>\n",
					xgetenv("BBWEB"), 
					bbh_item(clonewalk, BBH_PAGEPATH),
					bbh_item(clonewalk, BBH_HOSTNAME),
					bbh_item(clonewalk, BBH_PAGEPATHTITLE));
				addtobuffer(outbuf, msgline);
				clonewalk = next_host(clonewalk, 1);
				gotany++;
			}

			addtobuffer(outbuf, "</td>\n</tr>\n");
	
			hostwalk = clonewalk;
		}
		else {
			hostwalk = next_host(hostwalk, 0);
		}
	}
	regfree (&re); 	/*[wm] - free regex compiled patern */
	
	if (dojump) {
		if (gotany == 1) {
			printf("Location: %s%s\n\n", xgetenv("BBWEBHOST"), oneurl);
			return 0;
		}
		else if ((gotany > 1) && (gotonepage == OP_YES)) {
			printf("Location: %s%s/%s/\n\n", 
			       xgetenv("BBWEBHOST"), xgetenv("BBWEB"), onepage);
			return 0;
		}
	}

	print_header();
	if (!gotany) {
		printf("<tr><td align=left>%s</td><td align=left>Not found</td></tr>\n", pSearchPat);
	}
	else {
		printf("%s", grabstrbuffer(outbuf));
	}
	print_footer();

	/* [wm] - Free the strdup allocated memory */
	if (pSearchPat) xfree(pSearchPat);

	return 0;
}

