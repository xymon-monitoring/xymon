/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __URL_H__
#define __URL_H__

typedef struct urlelem_t {
	char *origform;
	char *scheme;
	char *schemeopts;
	char *host;
	char *ip;
	int  port;
	char *auth;
	char *relurl;
	int parseerror;
} urlelem_t;

enum webtesttype_t { 
	WEBTEST_PLAIN, WEBTEST_STATUS, WEBTEST_HEADER, WEBTEST_BODY, WEBTEST_POST, WEBTEST_SOAP
};

typedef struct weburl_t {
	enum webtesttype_t testtype;
	int reversetest;
	char *columnname;
	struct urlelem_t *desturl;
	unsigned char *postcontenttype;
	unsigned char *postdata;
	unsigned char *matchpattern;
} weburl_t;

extern char *urlunescape(char *url);
extern char *urldecode(char *envvar);
extern char *urlencode(char *s);
extern int urlvalidate(char *query, char *validchars);
extern char *cleanurl(char *url);
extern void parse_url(char *inputurl, urlelem_t *url);
extern char *decode_url(char *testspec, weburl_t *weburl);

extern void freeweburl_data(weburl_t *weburl);
extern void freeurlelem_data(struct urlelem_t *url);

#endif

