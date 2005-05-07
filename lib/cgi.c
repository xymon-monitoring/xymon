/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for handling CGI requests.                            */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: cgi.c,v 1.2 2005-05-07 06:21:57 henrik Exp $";

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
			lcgi_error("CGI violation - no QUERY_STRING\n");
			return NULL;
		}
	}

	dprintf("CGI: Request method='%s', data='%s'\n", method, reqdata);

	if (conttype && (strcasecmp(conttype, "application/x-www-form-urlencoded") == 0)) {
		token = strtok(reqdata, "&");

		while (token) {
			cgidata_t *newitem = (cgidata_t *)malloc(sizeof(cgidata_t));
			char *val;

			val = strchr(token, '='); if (val) { *val = '\0'; val++; }
			if (val) val = urlunescape(val);

			newitem->name = strdup(token);
			newitem->value = strdup(val);
			newitem->next = NULL;

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
	else {
		/* Raw data - return a single record to caller */
		head = (cgidata_t *)malloc(sizeof(cgidata_t));
		head->name = strdup("");
		head->value = strdup(reqdata);
		head->next = NULL;
	}

	return head;
}

