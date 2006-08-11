/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CGI_H__
#define __CGI_H__

typedef struct cgidata_t {
	char *name;
	char *value;
	char *filename;
	struct cgidata_t *next;
} cgidata_t;

enum cgi_method_t { CGI_OTHER, CGI_GET, CGI_POST };
extern enum cgi_method_t cgi_method;

extern char *cgi_error(void);
extern cgidata_t *cgi_request(void);
extern char *get_cookie(char *cookiename);

#endif

