/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@storner.dk>                 */
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

enum bbtesttype_t { 
	BBTEST_PLAIN, BBTEST_CONTENT, BBTEST_CONT, BBTEST_NOCONT, BBTEST_POST, BBTEST_NOPOST, BBTEST_TYPE, BBTEST_STATUS 
};

typedef struct bburl_t {
	int testtype;
	char *columnname;
	struct urlelem_t *desturl;
	struct urlelem_t *proxyurl;
	unsigned char *postcontenttype;
	unsigned char *postdata;
	unsigned char *expdata;
	unsigned char *okcodes;
	unsigned char *badcodes;
} bburl_t;

extern char *urlunescape(char *url);
extern char *urldecode(char *envvar);
extern char *urlencode(char *s);
extern int urlvalidate(char *query, char *validchars);
extern char *cleanurl(char *url);
extern void parse_url(char *inputurl, urlelem_t *url);
extern char *decode_url(char *testspec, bburl_t *bburl);

#endif

