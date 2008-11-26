/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for handling CGI requests.                            */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: cgi.c,v 1.8 2006-08-03 05:25:54 henrik Exp $";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "libbbgen.h"

#define MAX_REQ_SIZE (1024*1024)

enum cgi_method_t cgi_method = CGI_OTHER;

static char *cgi_error_text = NULL;
static void lcgi_error(char *msg)
{
	cgi_error_text = msg;
}

char *cgi_error(void)
{
	char *result = cgi_error_text;

	cgi_error_text = NULL;
	return result;
}

cgidata_t *cgi_request(void)
{
	char *method = NULL;
	char *reqdata = NULL;
	char *conttype = NULL;
        char *token;
	cgidata_t *head = NULL, *tail = NULL;

	cgi_error_text = NULL;
	cgi_method = CGI_OTHER;

	method = getenv("REQUEST_METHOD");
	if (!method) {
		lcgi_error("CGI violation - no REQUEST_METHOD\n");
		return NULL;
	}

	conttype = getenv("CONTENT_TYPE");

	if (strcasecmp(method, "POST") == 0) {
		char *contlen = getenv("CONTENT_LENGTH");
		int postsize = 0;

		cgi_method = CGI_POST;

		if (contlen) {
			postsize = atoi(contlen);
		}
		else {
			lcgi_error("CGI violation - no CONTENT_LENGTH\n");
			return NULL;
		}

		if (postsize < MAX_REQ_SIZE) {
			size_t n;

			reqdata = (char *)malloc(postsize+1);
			n = fread(reqdata, 1, postsize, stdin);
			if (n < postsize) {
				lcgi_error("Error reading POST data\n");
				return NULL;
			}
			reqdata[n] = '\0';
		}
		else {
			lcgi_error("Request too large\n");
			return NULL;
		}
	}
	else if (strcasecmp(method, "GET") == 0) {
		char *q = getenv("QUERY_STRING");

		cgi_method = CGI_GET;

		if (q) {
			if (strlen(q) < MAX_REQ_SIZE) {
				reqdata = strdup(q);
			}
			else {
				lcgi_error("Request too large\n");
				return NULL;
			}
		}
		else {
			/* This is OK - we may not have any query */
			return NULL;
		}
	}

	dbgprintf("CGI: Request method='%s', data='%s'\n", method, textornull(reqdata));

	if ((cgi_method == CGI_GET) || (conttype && (strcasecmp(conttype, "application/x-www-form-urlencoded") == 0))) {
		token = strtok(reqdata, "&");

		while (token) {
			cgidata_t *newitem = (cgidata_t *)calloc(1, sizeof(cgidata_t));
			char *val;

			val = strchr(token, '='); 
			if (val) { 
				*val = '\0'; 
				val = urlunescape(val+1);
			}

			newitem->name = strdup(token);
			newitem->value = strdup(val ? val : "");

			if (!tail) {
				head = newitem;
			}
			else {
				tail->next = newitem;
			}
			tail = newitem;

			token = strtok(NULL, "&");
		}
	}
	else if ((cgi_method == CGI_POST) && (conttype && (strcasecmp(conttype, "multipart/form-data") == 0))) {
		char *bol, *eoln, *delim;
		char eolnchar = '\n';
		char *currelembegin = NULL, *currelemend = NULL;
		cgidata_t *newitem = NULL;
		
		delim = reqdata;
		eoln = strchr(delim, '\n'); if (!eoln) return NULL;
		*eoln = '\0'; delim = strdup(reqdata); *eoln = '\n';
		if (*(delim + strlen(delim) - 1) == '\r') {
			eolnchar = '\r';
			*(delim + strlen(delim) - 1) = '\0';
		}

		bol = reqdata;
		do {
			eoln = strchr(bol, eolnchar); if (eoln) *eoln = '\0';
			if (strncmp(bol, delim, strlen(delim)) == 0) {
				if (newitem && currelembegin && (currelemend >= currelembegin)) {
					/* Finish off the current item */
					char savech;
					
					savech = *currelemend;
					*currelemend = '\0';
					newitem->value = strdup(currelembegin);
					*currelemend = savech;
					currelembegin = currelemend = NULL;
				}

				if (strcmp(bol+strlen(delim), "--") != 0) {
					/* New element */
					newitem = (cgidata_t *)calloc(1, sizeof(cgidata_t));

					if (!tail) head = newitem; else tail->next = newitem;
					tail = newitem;
				}
				else {
					/* No more elements, end of input */
					newitem = NULL;
					bol = NULL;
					continue;
				}
			}
			else if (newitem && (strncasecmp(bol, "Content-Disposition:", 20) == 0)) {
				char *tok;

				tok = strtok(bol, ";\t ");
				while (tok) {
					if (strncasecmp(tok, "name=", 5) == 0) {
						char *name;

						name = tok+5; 
						if (*name == '\"') {
							name++;
							*(name + strlen(name) - 1) = '\0';
						}
						newitem->name = strdup(name);
					}
					else if (strncasecmp(tok, "filename=", 9) == 0) {
						char *filename;

						filename = tok+9; 
						if (*filename == '\"') {
							filename++;
							*(filename + strlen(filename) - 1) = '\0';
						}
						newitem->filename = strdup(filename);
					}

					tok = strtok(NULL, ";\t ");
				}
			}
			else if (newitem && (strncasecmp(bol, "Content-Type:", 12) == 0)) {
			}
			else if (newitem && !currelembegin && (*bol == '\0')) {
				/* End of headers for one element */
				if (eoln) {
					currelembegin = eoln+1;
					if ((eolnchar == '\r') && (*currelembegin == '\n')) currelembegin++;
				}

				currelemend = currelembegin;
			}
			else if (newitem && currelembegin) {
				currelemend = (eoln ? eoln+1 : bol + strlen(bol));
			}

			if (eoln) {
				bol = eoln+1;
				if ((eolnchar == '\r') && (*bol == '\n')) bol++;
			}
			else {
				bol = NULL;
			}
		} while (bol && (*bol));

		if (newitem) {
			if (!newitem->name) newitem->name = "";
			if (!newitem->value) newitem->value = "";
		}
	}
	else {
		/* Raw data - return a single record to caller */
		head = (cgidata_t *)calloc(1, sizeof(cgidata_t));
		head->name = strdup("");
		head->value = strdup(reqdata);
	}

	if (reqdata) xfree(reqdata);

	return head;
}

char *get_cookie(char *cookiename)
{
	static char *ckdata = NULL;
	char *tok, *p;
	int n;

	/* If no cookie, just return NULL */
	p = getenv("HTTP_COOKIE");
	if (!p) return NULL;

	if (ckdata) xfree(ckdata);
	n = strlen(cookiename);

	/* Split the cookie variable into elements, separated by ";" and possible space. */
	ckdata = strdup(p);
	tok = strtok(ckdata, "; ");
	while (tok) {
		if ((strncmp(cookiename, tok, n) == 0) && (*(tok+n) == '=')) {
			/* Got it */
			return (tok+n+1);
		}

		tok = strtok(NULL, "; ");
	}

	xfree(ckdata); ckdata = NULL;
	return NULL;
}

