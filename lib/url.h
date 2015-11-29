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

extern int obeybbproxysyntax;

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
	WEBTEST_PLAIN, WEBTEST_CONTENT, WEBTEST_CONT, WEBTEST_CONTONLY, WEBTEST_NOCONT, WEBTEST_DATA, WEBTEST_DATAONLY, WEBTEST_DATASVC, WEBTEST_CLIENTHTTP, WEBTEST_CLIENTHTTPONLY, WEBTEST_CLIENTHTTPSVC, WEBTEST_POST, WEBTEST_NOPOST, WEBTEST_TYPE, WEBTEST_STATUS, WEBTEST_HEAD, WEBTEST_SOAP, WEBTEST_NOSOAP,
};

typedef struct weburl_t {
	int testtype;
	char *columnname;
	struct urlelem_t *desturl;
	struct urlelem_t *proxyurl;
	unsigned char *postcontenttype;
	unsigned char *postdata;
	unsigned char *expdata;
	unsigned char *okcodes;
	unsigned char *badcodes;
} weburl_t;

extern char *urlunescape(char *url);
extern char *urldecode(char *envvar);
extern char *urlencode(char *s);
extern int urlvalidate(char *query, char *validchars);
extern char *cleanurl(char *url);
extern void parse_url(char *inputurl, urlelem_t *url);
extern void free_urlelem_data(urlelem_t *url);
extern char *decode_url(char *testspec, weburl_t *weburl);

#endif

