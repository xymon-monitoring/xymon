/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the testing of HTTP service.                     */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: httptest.c,v 1.66 2004-08-20 20:53:29 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <sys/stat.h>
#include <netdb.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbtest-net.h"
#include "contest.h"
#include "httptest.h"
#include "dns.h"

typedef struct cookielist_t {
	char *host;
	int  tailmatch;
	char *path;
	int  secure;
	char *name;
	char *value;
	struct cookielist_t *next;
} cookielist_t;

static cookielist_t *cookiehead = NULL;

typedef struct loginlist_t {
	char *host;
	char *auth;
	struct loginlist_t *next;
} loginlist_t;

static loginlist_t *loginhead = NULL;


static void load_cookies(void)
{
	static int loaded = 0;

	char cookiefn[MAX_PATH];
	FILE *fd;
	char l[4096];
	char *c_host, *c_path, *c_name, *c_value;
	int c_tailmatch, c_secure;
	time_t c_expire;
	char *p;

	if (loaded) return;
	loaded = 1;

	sprintf(cookiefn, "%s/etc/cookies", getenv("BBHOME"));
	fd = fopen(cookiefn, "r");
	if (fd == NULL) return;

	c_host = c_path = c_name = c_value = NULL;
	c_tailmatch = c_secure = 0;
	c_expire = 0;

	while (fgets(l, sizeof(l), fd)) {
		p = strchr(l, '\n'); 
		if (p) {
			*p = '\0';
			p--;
			if ((p > l) && (*p == '\r')) *p = '\0';
		}

		if ((l[0] != '#') && strlen(l)) {
			int fieldcount = 0;
			p = strtok(l, "\t");
			if (p) { fieldcount++; c_host = p; p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_tailmatch = (strcmp(p, "TRUE") == 0); p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_path = p; p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_secure = (strcmp(p, "TRUE") == 0); p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_expire = atol(p); p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_name = p; p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_value = p; p = strtok(NULL, "\t"); }
			if ((fieldcount == 7) && (c_expire > time(NULL))) {
				/* We have a valid cookie */
				cookielist_t *ck = (cookielist_t *)malloc(sizeof(cookielist_t));
				ck->host = malcop(c_host);
				ck->tailmatch = c_tailmatch;
				ck->path = malcop(c_path);
				ck->secure = c_secure;
				ck->name = malcop(c_name);
				ck->value = malcop(c_value);
				ck->next = cookiehead;
				cookiehead = ck;
			}
		}
	}

	fclose(fd);
}

static void load_netrc(void)
{

#define WANT_TOKEN   0
#define MACHINEVAL   1
#define LOGINVAL     2
#define PASSVAL      3
#define OTHERVAL     4

	static int loaded = 0;

	char netrcfn[MAX_PATH];
	FILE *fd;
	char l[4096];
	char *host, *login, *password, *p;
	int state = WANT_TOKEN;

	if (loaded) return;
	loaded = 1;

	sprintf(netrcfn, "%s/.netrc", getenv("HOME"));
	fd = fopen(netrcfn, "r");
	if (fd == NULL) return;

	host = login = password = NULL;
	while (fgets(l, sizeof(l), fd)) {
		p = strchr(l, '\n'); 
		if (p) {
			*p = '\0';
			p--;
			if ((p > l) && (*p == '\r')) *p = '\0';
		}

		if ((l[0] != '#') && strlen(l)) {
			p = strtok(l, " \t");
			while (p) {
				switch (state) {
				  case WANT_TOKEN:
					if (strcmp(p, "machine") == 0) state = MACHINEVAL;
					else if (strcmp(p, "login") == 0) state = LOGINVAL;
					else if (strcmp(p, "password") == 0) state = PASSVAL;
					else if (strcmp(p, "account") == 0) state = OTHERVAL;
					else if (strcmp(p, "macdef") == 0) state = OTHERVAL;
					else if (strcmp(p, "default") == 0) { host = ""; state = WANT_TOKEN; }
					else state = WANT_TOKEN;
					break;

				  case MACHINEVAL:
					host = malcop(p); state = WANT_TOKEN; break;

				  case LOGINVAL:
					login = malcop(p); state = WANT_TOKEN; break;

				  case PASSVAL:
					password = malcop(p); state = WANT_TOKEN; break;

				  case OTHERVAL:
				  	state = WANT_TOKEN; break;
				}

				if (host && login && password) {
					loginlist_t *item = (loginlist_t *) malloc(sizeof(loginlist_t));

					item->host = host;
					item->auth = (char *) malloc(strlen(login) + strlen(password) + 2);
					sprintf(item->auth, "%s:%s", login, password);
					item->next = loginhead;
					loginhead = item;
					host = login = password = NULL;
				}

				p = strtok(NULL, " \t");
			}
		}
	}

	fclose(fd);
}

int parse_url(url_t *url, char *inputurl)
{
	/*
	 * See RFC1808 for guidelines to parsing a URL
	 */

	char *tempurl = malcop(inputurl);
	char *fragment = NULL;
	char *scheme;
	char *netloc;
	char *startp, *p;
	int result = 0;

	fragment = strchr(tempurl, '#'); if (fragment) *fragment = '\0';

	startp = tempurl;
	p = strchr(startp, ':');
	if (p) {
		scheme = startp;
		*p = '\0';
		startp = (p+1);
	}
	else scheme = "http";

	if (strncmp(startp, "//", 2) == 0) {
		startp += 2;
		netloc = startp;

		p = strchr(startp, '/');
		if (p) {
			*p = '\0';
			startp = (p+1);
		}
		else startp += strlen(startp);
	}
	else {
		result = 1;
		netloc = "";
		errprintf("Malformed URL missing '//' in '%s'\n", inputurl);
	}

	url->protocol = malcop(scheme);

	/* netloc is [username:password@]hostname[:port] */
	url->auth = NULL; url->port = ((strcmp(scheme, "https") == 0) ? 443 : 80);
	p = strchr(netloc, '@');
	if (p) {
		*p = '\0';
		url->auth = malcop(netloc);
		netloc = (p+1);
	}
	p = strchr(netloc, ':');
	if (p) {
		*p = '\0';
		url->port = atoi(p+1);
	}

	url->ip = "";
	url->host = malcop(netloc);

	if (strlen(url->host)) {
		char *dnsip;

		dnsip = dnsresolve(url->host);
		if (dnsip) {
			url->ip = malcop(dnsip);
		}
		else {
			result = 2;
			dprintf("Could not resolve URL hostname '%s'\n", url->host);
		}
	}

	if (fragment) *fragment = '#';
	url->relurl = malloc(strlen(startp) + 2);
	sprintf(url->relurl, "/%s", startp);

	if (url->auth == NULL) {
		/* See if we have it in the .netrc list */
		loginlist_t *walk;

		for (walk = loginhead; (walk && (strcmp(walk->host, url->host) != 0)); walk = walk->next) ;
		if (walk) url->auth = walk->auth;
	}

	free(tempurl);
	return result;
}


char *base64encode(unsigned char *buf)
{
	static char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned char c0, c1, c2;
	unsigned int n0, n1, n2, n3;
	unsigned char *inp, *outp;
	unsigned char *result;

	result = malloc(4*(strlen(buf)/3 + 1) + 1);
	inp = buf; outp=result;

	while (strlen(inp) >= 3) {
		c0 = *inp; c1 = *(inp+1); c2 = *(inp+2);

		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */
		n2 = ((c1 & 15) << 2) + (c2 >> 6);	/* 4 bits from c1, 2 bits from c2 */
		n3 = (c2 & 63);				/* 6 bits from c2 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = b64chars[n2]; outp++;
		*outp = b64chars[n3]; outp++;

		inp += 3;
	}

	if (strlen(inp) == 1) {
		c0 = *inp; c1 = 0;
		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = '='; outp++;
		*outp = '='; outp++;
	}
	else if (strlen(inp) == 2) {
		c0 = *inp; c1 = *(inp+1); c2 = 0;

		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */
		n2 = ((c1 & 15) << 2) + (c2 >> 6);	/* 4 bits from c1, 2 bits from c2 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = b64chars[n2]; outp++;
		*outp = '='; outp++;
	}

	*outp = '\0';

	return result;
}


int tcp_http_data_callback(unsigned char *buf, unsigned int len, void *priv)
{
	/*
	 * This callback receives data from HTTP servers.
	 * While doing that, it splits out the data into a
	 * buffer for the HTTP headers, and a buffer for the
	 * HTTP content-data.
	 */

	http_data_t *item = (http_data_t *) priv;

	if (item->gotheaders) {
		unsigned int len1chunk = 0;
		int i;

		/*
		 * We already have the headers, so just stash away the data
		 */


		while (len > 0) {
			dprintf("HDC IN : state=%d, leftinchunk=%d, len=%d\n", item->chunkstate, item->leftinchunk, len);
			switch (item->chunkstate) {
			  case CHUNK_NOTCHUNKED:
				len1chunk = len;
				if ((item->contlen > 0) && (item->contlen >= len)) item->contlen -= len;
				break;

			  case CHUNK_INIT:
				/* We're about to pick up a chunk length */
				item->leftinchunk = 0;
				item->chunkstate = CHUNK_GETLEN;
				len1chunk = 0;
				break;

			  case CHUNK_GETLEN:
				/* We are collecting the length of the chunk */
				i = hexvalue(*buf);
				if (i == -1) {
					item->chunkstate = CHUNK_SKIPLENCR;
				}
				else {
					item->leftinchunk = item->leftinchunk*16 + i;
					buf++; len--;
				}
				len1chunk = 0;
				break;
				
			  case CHUNK_SKIPLENCR:
				/* We've got the length, now skip to the next LF */
				if (*buf == '\n') {
					buf++; len--; 
					item->chunkstate = ((item->leftinchunk > 0) ? CHUNK_DATA : CHUNK_NOMORE);
				}
				else if ((*buf == '\r') || (*buf == ' ')) {
					buf++; len--;
				}
				else {
					errprintf("Yikes - strange data following chunk len. Saw a '%c'\n", *buf);
					buf++; len--;
				}
				len1chunk = 0;
				break;

			  case CHUNK_DATA:
				/* Passing off the data */
				if (len > item->leftinchunk) len1chunk = item->leftinchunk;
				else len1chunk = len;
				item->leftinchunk -= len1chunk;
				if (item->leftinchunk == 0) item->chunkstate = CHUNK_SKIPENDCR;
				break;

			  case CHUNK_SKIPENDCR:
				/* Skip CR/LF after a chunk */
				if (*buf == '\n') {
					buf++; len--; item->chunkstate = CHUNK_DONE;
				}
				else if (*buf == '\r') {
					buf++; len--;
				}
				else {
					errprintf("Yikes - strange data following chunk data. Saw a '%c'\n", *buf);
					buf++; len--;
				}
				len1chunk = 0;
				break;

			  case CHUNK_DONE:
				/* One chunk is done, continue with the next */
				len1chunk = 0;
				item->chunkstate = CHUNK_GETLEN;
				break;

			  case CHUNK_NOMORE:
				/* All chunks done. Skip the rest (trailers) */
				len1chunk = 0;
				len = 0;
			}

			if (len1chunk > 0) {
				switch (item->contentcheck) {
				  case CONTENTCHECK_NONE:
				  case CONTENTCHECK_CONTENTTYPE:
					/* No need to save output - just drop it */
					break;

				  case CONTENTCHECK_REGEX:
				  case CONTENTCHECK_NOREGEX:
					/* Save the full data */
					if ((item->output == NULL) || (item->outlen == 0)) {
						item->output = (unsigned char *)malloc(len1chunk+1);
					}
					else {
						item->output = (unsigned char *)realloc(item->output, item->outlen+len1chunk+1);
					}

					memcpy(item->output+item->outlen, buf, len1chunk);
					item->outlen += len1chunk;
					*(item->output + item->outlen) = '\0'; /* Just in case ... */
					break;

				  case CONTENTCHECK_DIGEST:
					/* Run the data through our digest routine, but discard the raw data */
					if ((item->digestctx == NULL) || (digest_data(item->digestctx, buf, len1chunk) != 0)) {
						errprintf("Failed to hash data for digest\n");
					}
					break;
				}

				buf += len1chunk;
				len -= len1chunk;
				dprintf("HDC OUT: state=%d, leftinchunk=%d, len=%d\n", item->chunkstate, item->leftinchunk, len);
			}
		}
	}
	else {
		/*
		 * Havent seen the end of headers yet.
		 */
		unsigned char *p;

		/* First, add this to the header-buffer */
		if (item->headers == NULL) {
			item->headers = (unsigned char *) malloc(len+1);
		}
		else {
			item->headers = (unsigned char *) realloc(item->headers, item->hdrlen+len+1);
		}

		memcpy(item->headers+item->hdrlen, buf, len);
		item->hdrlen += len;
		*(item->headers + item->hdrlen) = '\0';

		/* 
		 * Now see if we have the end-of-headers delimiter.
		 * This SHOULD be <cr><lf><cr><lf>, but RFC 2616 says
		 * you SHOULD recognize just plain <lf><lf>.
		 * So we loop, looking for the next LF in the input,
		 * if there is one and the next is <cr> then just skip
		 * the <cr>; repeat until a second <lf> is seen or we
		 * hit end-of-buffer.
		 */
		p=item->headers;
		do {
			p = strchr(p, '\n');
			if (p) {
				p++;
				if (*p == '\r') p++;
			}
		} while (p && (*p != '\n'));

		if (p) {
			unsigned int bytesinheaders, bytesindata;
			unsigned int delimchars = 1;
			char *p1, *xferencoding;
			int contlen;

			/* We did find the end-of-header delim. */
			item->gotheaders = 1;
			if (*(p-1) == '\r') { *(p-1) = '\0'; delimchars++; } /* NULL-terminate the headers. */
			*p = '\0';
			p++;

			/* See if the transfer uses chunks */
			p1 = item->headers; xferencoding = NULL; contlen = 0;
			do {
				if (strncasecmp(p1, "Transfer-encoding:", 18) == 0) {
					p1 += 18; while (isspace(*p1)) p1++;
					xferencoding = p1;
				}
				else if (strncasecmp(p1, "Content-Length:", 15) == 0) {
					p1 += 15; while (isspace(*p1)) p1++;
					contlen = atoi(p1);
				}
				else {
					p1 = strchr(p1, '\n'); if (p1) p1++;
				}
			} while (p1 && (xferencoding == NULL));

			if (xferencoding && (strncasecmp(xferencoding, "chunked", 7) == 0)) {
				item->chunkstate = CHUNK_INIT;
			}
			item->contlen = (contlen ? contlen : -1);

			bytesinheaders = ((p - item->headers) - delimchars);
			bytesindata = item->hdrlen - bytesinheaders - delimchars;
			item->hdrlen = bytesinheaders;
			if (*p) {
				/* 
				 * We received some content data together with the
				 * headers. Save these to the content-data area.
				 */
				tcp_http_data_callback(p, bytesindata, priv);
			}
		}
	}

	if (item->chunkstate == CHUNK_NOTCHUNKED) 
		/* Not chunked - we're done if contlen reaches 0 */
		return (item->contlen == 0);
	else 
		/* Chunked - we're done if we reach state NOMORE*/
		return (item->chunkstate == CHUNK_NOMORE);
}

void tcp_http_final_callback(void *priv)
{
	/*
	 * This callback is invoked when a HTTP request is
	 * complete (when the socket is closed).
	 * We use it to pickup some information from the raw
	 * HTTP response, and parse it into some easier to
	 * handle properties.
	 */

	http_data_t *item = (http_data_t *) priv;

	if ((item->contentcheck == CONTENTCHECK_DIGEST) && item->digestctx) {
		item->digest = digest_done(item->digestctx);
	}

	if (item->headers) {
		int http1subver;
		char *p;

		sscanf(item->headers, "HTTP/1.%d %ld", &http1subver, &item->httpstatus);

		item->contenttype = NULL;
		p = item->headers;
		do {
			if (strncasecmp(p, "Content-Type:", 13) == 0) {
				char *p2, savechar;

				p += 13; while (isspace(*p)) p++;
				p2 = (p + strcspn(p, "\r\n ;"));
				savechar = *p2; *p2 = '\0';
				item->contenttype = malcop(p);
				*p2 = savechar;
			}
			else {
				p = strchr(p, '\n'); if (p) p++;
			}
		} while ((item->contenttype == NULL) && p);
	}

	if (item->tcptest->errcode != CONTEST_ENOERROR) {
		/* Flag error by setting httpstatus to 0 */
		item->httpstatus = 0;
	}
}


void add_http_test(testitem_t *t)
{
	/* See http://www.openssl.org/docs/apps/ciphers.html for cipher strings */
	static char *ciphersmedium = "MEDIUM";	/* Must be formatted for openssl library */
	static char *ciphershigh = "HIGH";	/* Must be formatted for openssl library */

	http_data_t *httptest;

	char *proto = NULL;
	int  httpversion = HTTPVER_11;
	char *postdata = NULL;

	ssloptions_t *sslopt = NULL;
	char *sslopt_ciphers = NULL;
	int sslopt_version = SSLVERSION_DEFAULT;

	char *proxy = NULL;
	char *proxyauth = NULL;

	char *forcedip = NULL;

	char *httprequest = NULL;
	int httprequestlen;
	url_t url, proxyurl;
	int proxystatus, urlstatus;

	cookielist_t *ck;
	int firstcookie = 1;

	/* 
	 * t->testspec containts the full testspec
	 * It can be either "URL", "content=URL", 
	 * "cont;URL;expected_data", "post;URL;postdata;expected_data"
	 */

	load_cookies();
	load_netrc();

	/* Allocate the private data and initialize it */
	httptest = (http_data_t *) calloc(1, sizeof(http_data_t));
	t->privdata = (void *) httptest;

	httptest->url = malcop(realurl(t->testspec, &proxy, &proxyauth, &forcedip, NULL));
	proxystatus = urlstatus = 0;
	if (proxy) proxystatus = parse_url(&proxyurl, proxy);
	urlstatus = parse_url(&url, httptest->url);
	httptest->parsestatus = (proxystatus ? proxystatus : urlstatus);
	httptest->contlen = -1;

	/* 
	 * Determine the content- and post-data (if any).
	 * Sets:
	 *   httptest->contentcheck
	 *   httptest->exp
	 *   proto
	 *   postdata
	 */
	if (strncmp(t->testspec, "content=", 8) == 0) {
		FILE *contentfd;
		char contentfn[200];
		char l[MAX_LINE_LEN];
		char *p;

		sprintf(contentfn, "%s/content/%s.substring", getenv("BBHOME"), commafy(t->host->hostname));
		contentfd = fopen(contentfn, "r");
		if (contentfd) {
			if (fgets(l, sizeof(l), contentfd)) {
				int status;

				p = strchr(l, '\n'); if (p) { *p = '\0'; };
				httptest->exp = (void *) malloc(sizeof(regex_t));
				status = regcomp((regex_t *)httptest->exp, l, REG_EXTENDED|REG_NOSUB);
				if (status) {
					errprintf("Failed to compile regexp '%s' for URL %s\n", p, httptest->url);
					httptest->contstatus = STATUS_CONTENTMATCH_BADREGEX;
				}
			}
			else {
				httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;
			}
			fclose(contentfd);
		}
		else {
			httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;
		}
		proto = t->testspec + 8;
		httptest->contentcheck = CONTENTCHECK_REGEX;
	}
	else if (strncmp(t->testspec, "cont;", 5) == 0) {
		char *p = strrchr(t->testspec, ';');
		if (p) {
			if ( *(p+1) == '#' ) {
				char *q;

				httptest->contentcheck = CONTENTCHECK_DIGEST;
				httptest->exp = (void *) malcop(p+2);
				q = strchr(httptest->exp, ':');
				if (q) {
					*q = '\0';
					httptest->digestctx = digest_init(httptest->exp);
					*q = ':';
				}
			}
			else {
				int status;

				httptest->contentcheck = CONTENTCHECK_REGEX;
				httptest->exp = (void *) malloc(sizeof(regex_t));
				status = regcomp((regex_t *)httptest->exp, p+1, REG_EXTENDED|REG_NOSUB);
				if (status) {
					errprintf("Failed to compile regexp '%s' for URL %s\n", p+1, httptest->url);
					httptest->contstatus = STATUS_CONTENTMATCH_BADREGEX;
				}
			}
		}
		else httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;
		proto = t->testspec+5;
	}
	else if (strncmp(t->testspec, "nocont;", 7) == 0) {
		char *p = strrchr(t->testspec, ';');
		if (p) {
			int status;

			httptest->contentcheck = CONTENTCHECK_NOREGEX;
			httptest->exp = (void *) malloc(sizeof(regex_t));
			status = regcomp((regex_t *)httptest->exp, p+1, REG_EXTENDED|REG_NOSUB);
			if (status) {
				errprintf("Failed to compile regexp '%s' for URL %s\n", p+1, httptest->url);
				httptest->contstatus = STATUS_CONTENTMATCH_BADREGEX;
			}
		}
		else httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;
		proto = t->testspec+5;
	}
	else if (strncmp(t->testspec, "post;", 5) == 0) {
		/* POST request - whee! */

		/* First grab data we expect back, like with "cont;" */
		char *p = strrchr(t->testspec, ';');
		char *q;

		if (p) {
			/* It is legal not to specify anything for the expected output from a POST */
			if (strlen(p+1) > 0) {
				if (*(p+1) == '#') {
					char *q;

					httptest->contentcheck = CONTENTCHECK_DIGEST;
					httptest->exp = (void *) malcop(p+2);
					q = strchr(httptest->exp, ':');
					if (q) {
						*q = '\0';
						httptest->digestctx = digest_init(httptest->exp);
						*q = ':';
					}
				}
				else {
					int status;

					httptest->contentcheck = CONTENTCHECK_REGEX;
					httptest->exp = (void *) malloc(sizeof(regex_t));
					status = regcomp((regex_t *)httptest->exp, p+1, REG_EXTENDED|REG_NOSUB);
					if (status) {
						errprintf("Failed to compile regexp '%s' for URL %s\n", p+1, httptest->url);
						httptest->contstatus = STATUS_CONTENTMATCH_BADREGEX;
					}
				}
			}
		}
		else httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;

		if (p) {
			*p = '\0';  /* Cut off expected data */
			q = strrchr(t->testspec, ';');
			if (q) postdata = malcop(q+1);
			*p = ';';  /* Restore testspec */
		}

		proto = t->testspec+5;
	}
	else if (strncmp(t->testspec, "nopost;", 7) == 0) {
		/* POST request - whee! */

		/* First grab data we expect back, like with "cont;" */
		char *p = strrchr(t->testspec, ';');
		char *q;

		if (p) {
			/* It is legal not to specify anything for the expected output from a POST */
			if (strlen(p+1) > 0) {
				int status;

				httptest->contentcheck = CONTENTCHECK_NOREGEX;
				httptest->exp = (void *) malloc(sizeof(regex_t));
				status = regcomp((regex_t *)httptest->exp, p+1, REG_EXTENDED|REG_NOSUB);
				if (status) {
					errprintf("Failed to compile regexp '%s' for URL %s\n", p+1, httptest->url);
					httptest->contstatus = STATUS_CONTENTMATCH_BADREGEX;
				}
			}
		}
		else httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;

		if (p) {
			*p = '\0';  /* Cut off expected data */
			q = strrchr(t->testspec, ';');
			if (q) postdata = malcop(q+1);
			*p = ';';  /* Restore testspec */
		}

		proto = t->testspec+7;
	}
	else if (strncmp(t->testspec, "type;", 5) == 0) {
		char *p = strrchr(t->testspec, ';');
		if (p) {
			httptest->contentcheck = CONTENTCHECK_CONTENTTYPE;
			httptest->exp = (void *) malcop(p+1);
		}
		else httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;
		proto = t->testspec+5;
	}
	else {
		proto = t->testspec;
	}

	if      (strncmp(proto, "https3:", 7) == 0)      sslopt_version = SSLVERSION_V3;
	else if (strncmp(proto, "https2:", 7) == 0)      sslopt_version = SSLVERSION_V2;
	else if (strncmp(proto, "httpsh:", 7) == 0)      sslopt_ciphers = ciphershigh;
	else if (strncmp(proto, "httpsm:", 7) == 0)      sslopt_ciphers = ciphersmedium;
	else if (strncmp(proto, "http10:", 7) == 0)      httpversion    = HTTPVER_10;
	else if (strncmp(proto, "http11:", 7) == 0)      httpversion    = HTTPVER_11;

	if (sslopt_ciphers || (sslopt_version != SSLVERSION_DEFAULT)){
		sslopt = (ssloptions_t *) malloc(sizeof(ssloptions_t));
		sslopt->cipherlist = sslopt_ciphers;
		sslopt->sslversion = sslopt_version;
	}

	/* Generate the request */
	addtobuffer(&httprequest, &httprequestlen, (postdata ? "POST " : "GET "));
	switch (httpversion) {
		case HTTPVER_10: 
			addtobuffer(&httprequest, &httprequestlen, (proxy ? httptest->url : url.relurl));
			addtobuffer(&httprequest, &httprequestlen, " HTTP/1.0\r\n"); 
			break;

		case HTTPVER_11: 
			/*
			 * Experience shows that even though HTTP/1.1 says you should send the
			 * full URL, some servers (e.g. SunOne App server 7) choke on it.
			 * So just send the good-old relative URL unless we're proxying.
			 */
			addtobuffer(&httprequest, &httprequestlen, (proxy ? httptest->url : url.relurl));
			addtobuffer(&httprequest, &httprequestlen, " HTTP/1.1\r\n"); 
			addtobuffer(&httprequest, &httprequestlen, "Connection: close\r\n"); 
			break;
	}

	addtobuffer(&httprequest, &httprequestlen, "Host: ");
	addtobuffer(&httprequest, &httprequestlen, url.host);
	addtobuffer(&httprequest, &httprequestlen, "\r\n");

	if (postdata) {
		char contlenhdr[100];

		sprintf(contlenhdr, "Content-Length: %d\r\n", strlen(postdata));
		addtobuffer(&httprequest, &httprequestlen, contlenhdr);
		addtobuffer(&httprequest, &httprequestlen, "Content-Type: application/x-www-form-urlencoded\r\n");
	}
	{
		char useragent[100];

		sprintf(useragent, "User-Agent: BigBrother bbtest-net/%s\r\n", VERSION);
		addtobuffer(&httprequest, &httprequestlen, useragent);
	}
	if (url.auth) {
		addtobuffer(&httprequest, &httprequestlen, "Authorization: Basic ");
		addtobuffer(&httprequest, &httprequestlen, base64encode(url.auth));
		addtobuffer(&httprequest, &httprequestlen, "\r\n");
	}
	if (proxy && proxyauth) {
		addtobuffer(&httprequest, &httprequestlen, "Proxy-Authorization: ");
		addtobuffer(&httprequest, &httprequestlen, base64encode(proxyauth));
		addtobuffer(&httprequest, &httprequestlen, "\r\n");
	}
	for (ck = cookiehead; (ck); ck = ck->next) {
		int useit = 0;

		if (ck->tailmatch) {
			int startpos = strlen(url.host) - strlen(ck->host);

			if (startpos > 0) useit = (strcmp(url.host+startpos, ck->host) == 0);
		}
		else useit = (strcmp(url.host, ck->host) == 0);
		if (useit) useit = (strncmp(ck->path, url.relurl, strlen(ck->path)) == 0);

		if (useit) {
			if (firstcookie) {
				addtobuffer(&httprequest, &httprequestlen, "Cookie: ");
				firstcookie = 0;
			}
			addtobuffer(&httprequest, &httprequestlen, ck->name);
			addtobuffer(&httprequest, &httprequestlen, "=");
			addtobuffer(&httprequest, &httprequestlen, ck->value);
			addtobuffer(&httprequest, &httprequestlen, "\r\n");
		}
	}

	/* Some standard stuff */
	addtobuffer(&httprequest, &httprequestlen, "Accept: */*\r\n");
	addtobuffer(&httprequest, &httprequestlen, "Pragma: no-cache\r\n");

	/* The final blank line terminates the headers */
	addtobuffer(&httprequest, &httprequestlen, "\r\n");

	/* Post data goes last */
	if (postdata) addtobuffer(&httprequest, &httprequestlen, postdata);

	/* Add to TCP test queue */
	httptest->tcptest = add_tcp_test((proxy ? proxyurl.ip       : (forcedip ? forcedip : url.ip)), 
					 (proxy ? proxyurl.port     : url.port), 
				    	 (proxy ? proxyurl.protocol : url.protocol), 
					 sslopt, 0, httprequest, 
					 httptest, tcp_http_data_callback, tcp_http_final_callback);
}
