/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* This is used to implement the testing of HTTP service.                     */
/*                                                                            */
/* Copyright (C) 2003-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: httptest.c,v 1.87 2006-07-20 16:06:41 henrik Exp $";

#include <sys/types.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <sys/stat.h>

#include "version.h"
#include "libbbgen.h"

#include "bbtest-net.h"
#include "contest.h"
#include "httpcookies.h"
#include "httptest.h"
#include "dns.h"


int tcp_http_data_callback(unsigned char *buf, unsigned int len, void *priv)
{
	/*
	 * This callback receives data from HTTP servers.
	 * While doing that, it splits out the data into a
	 * buffer for the HTTP headers, and a buffer for the
	 * HTTP content-data.
	 * Return 1 if data is complete, 0 if more data wanted.
	 */

	http_data_t *item = (http_data_t *) priv;

	if (item->gotheaders) {
		unsigned int len1chunk = 0;
		int i;

		/*
		 * We already have the headers, so just stash away the data
		 */


		while (len > 0) {
			dbgprintf("HDC IN : state=%d, leftinchunk=%d, len=%d\n", item->chunkstate, item->leftinchunk, len);
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
				dbgprintf("HDC OUT: state=%d, leftinchunk=%d, len=%d\n", item->chunkstate, item->leftinchunk, len);
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

check_for_endofheaders:
		/* 
		 * Now see if we have the end-of-headers delimiter.
		 * This SHOULD be <cr><lf><cr><lf>, but RFC 2616 says
		 * you SHOULD recognize just plain <lf><lf>.
		 * So try the second form, if the first one is not there.
		 */
		p=strstr(item->headers, "\r\n\r\n");
		if (p) {
			p += 4;
		}
		else {
			p = strstr(item->headers, "\n\n");
			if (p) p += 2;
		}

		if (p) {
			int http1subver, httpstatus;
			unsigned int bytesindata;
			char *p1, *xferencoding;
			int contlen;

			/* We have an end-of-header delimiter, but it could be just a "100 Continue" response */
			sscanf(item->headers, "HTTP/1.%d %d", &http1subver, &httpstatus);
			if (httpstatus == 100) {
				/* 
				 * It's a "100"  continue-status.
				 * Just drop this set of headers, and move on.
				 */
				item->hdrlen -= (p - item->headers);
				if (item->hdrlen > 0) {
					memmove(item->headers, p, item->hdrlen);
					*(item->headers + item->hdrlen) = '\0';
					goto check_for_endofheaders;
				}
				else {
					xfree(item->headers);
					item->headers = NULL;
					item->hdrlen = 0;
					return 0;
				}

				/* Should never go here ... */
			}


			/* We did find the end-of-header delimiter, and it is not a "100" status. */
			item->gotheaders = 1;

			/* p points at the first byte of DATA. So the header ends 1 or 2 bytes before. */
			*(p-1) = '\0';
			if (*(p-2) == '\r') { *(p-2) = '\0'; } /* NULL-terminate the headers. */

			/* See if the transfer uses chunks */
			p1 = item->headers; xferencoding = NULL; contlen = 0;
			do {
				if (strncasecmp(p1, "Transfer-encoding:", 18) == 0) {
					p1 += 18; while (isspace((int)*p1)) p1++;
					xferencoding = p1;
				}
				else if (strncasecmp(p1, "Content-Length:", 15) == 0) {
					p1 += 15; while (isspace((int)*p1)) p1++;
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

			bytesindata = item->hdrlen - (p - item->headers);
			item->hdrlen = strlen(item->headers);
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

				p += 13; while (isspace((int)*p)) p++;
				p2 = (p + strcspn(p, "\r\n ;"));
				savechar = *p2; *p2 = '\0';
				item->contenttype = strdup(p);
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
	http_data_t *httptest;

	char *dnsip = NULL;
	ssloptions_t *sslopt = NULL;
	char *sslopt_ciphers = NULL;
	int sslopt_version = SSLVERSION_DEFAULT;
	char *sslopt_clientcert = NULL;
	int  httpversion = HTTPVER_11;
	cookielist_t *ck = NULL;
	int firstcookie = 1;
	char *decodedurl;
	strbuffer_t *httprequest = newstrbuffer(0);

	/* Allocate the private data and initialize it */
	httptest = (http_data_t *) calloc(1, sizeof(http_data_t));
	t->privdata = (void *) httptest;

	decodedurl = decode_url(t->testspec, &httptest->bburl);
	if (!decodedurl) {
		errprintf("Invalid URL for http check: %s\n", t->testspec);
		return;
	}

	httptest->url = strdup(decodedurl);
	httptest->contlen = -1;
	httptest->parsestatus = (httptest->bburl.proxyurl ? httptest->bburl.proxyurl->parseerror : httptest->bburl.desturl->parseerror);

	/* If there was a parse error in the URL, dont run the test */
	if (httptest->parsestatus) return;


	if (httptest->bburl.proxyurl && (httptest->bburl.proxyurl->ip == NULL)) {
		dnsip = dnsresolve(httptest->bburl.proxyurl->host);
		if (dnsip) {
			httptest->bburl.proxyurl->ip = strdup(dnsip);
		}
		else {
			dbgprintf("Could not resolve URL hostname '%s'\n", httptest->bburl.proxyurl->host);
		}
	}
	else if (httptest->bburl.desturl->ip == NULL) {
		dnsip = dnsresolve(httptest->bburl.desturl->host);
		if (dnsip) {
			httptest->bburl.desturl->ip = strdup(dnsip);
		}
		else {
			dbgprintf("Could not resolve URL hostname '%s'\n", httptest->bburl.desturl->host);
		}
	}

	switch (httptest->bburl.testtype) {
	  case BBTEST_PLAIN:
	  case BBTEST_STATUS:
		httptest->contentcheck = CONTENTCHECK_NONE;
		break;

	  case BBTEST_CONTENT:
		{
			FILE *contentfd;
			char contentfn[PATH_MAX];
			sprintf(contentfn, "%s/content/%s.substring", xgetenv("BBHOME"), commafy(t->host->hostname));
			contentfd = fopen(contentfn, "r");
			if (contentfd) {
				char l[MAX_LINE_LEN];
				char *p;

				if (fgets(l, sizeof(l), contentfd)) {
					p = strchr(l, '\n'); if (p) { *p = '\0'; };
					httptest->bburl.expdata = strdup(l);
				}
				else {
					httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;
				}
				fclose(contentfd);
			}
			else {
				httptest->contstatus = STATUS_CONTENTMATCH_NOFILE;
			}
			httptest->contentcheck = CONTENTCHECK_REGEX;
		}
		break;

	  case BBTEST_CONT:
		httptest->contentcheck = ((*httptest->bburl.expdata == '#') ?  CONTENTCHECK_DIGEST : CONTENTCHECK_REGEX);
		break;

	  case BBTEST_NOCONT:
		httptest->contentcheck = CONTENTCHECK_NOREGEX;
		break;

	  case BBTEST_POST:
	  case BBTEST_SOAP:
		if (httptest->bburl.expdata == NULL) {
			httptest->contentcheck = CONTENTCHECK_NONE;
		}
		else {
			httptest->contentcheck = ((*httptest->bburl.expdata == '#') ?  CONTENTCHECK_DIGEST : CONTENTCHECK_REGEX);
		}
		break;

	  case BBTEST_NOPOST:
	  case BBTEST_NOSOAP:
		if (httptest->bburl.expdata == NULL) {
			httptest->contentcheck = CONTENTCHECK_NONE;
		}
		else {
			httptest->contentcheck = CONTENTCHECK_NOREGEX;
		}
		break;

	  case BBTEST_TYPE:
		httptest->contentcheck = CONTENTCHECK_CONTENTTYPE;
		break;
	}

	/* Compile the hashes and regex's for those tests that use it */
	switch (httptest->contentcheck) {
	  case CONTENTCHECK_DIGEST:
		{
			char *hashfunc;

			httptest->exp = (void *) strdup(httptest->bburl.expdata+1);
			hashfunc = strchr(httptest->exp, ':');
			if (hashfunc) {
				*hashfunc = '\0';
				httptest->digestctx = digest_init(httptest->exp);
				*hashfunc = ':';
			}
		}
		break;

	  case CONTENTCHECK_REGEX:
	  case CONTENTCHECK_NOREGEX:
		{
			int status;

			httptest->exp = (void *) malloc(sizeof(regex_t));
			status = regcomp((regex_t *)httptest->exp, httptest->bburl.expdata, REG_EXTENDED|REG_NOSUB);
			if (status) {
				errprintf("Failed to compile regexp '%s' for URL %s\n", httptest->bburl.expdata, httptest->url);
				httptest->contstatus = STATUS_CONTENTMATCH_BADREGEX;
			}
		}
		break;

	  case CONTENTCHECK_CONTENTTYPE:
		httptest->exp = httptest->bburl.expdata;
		break;
	}

	if (httptest->bburl.desturl->schemeopts) {
		if      (strstr(httptest->bburl.desturl->schemeopts, "3"))      sslopt_version = SSLVERSION_V3;
		else if (strstr(httptest->bburl.desturl->schemeopts, "2"))      sslopt_version = SSLVERSION_V2;

		if      (strstr(httptest->bburl.desturl->schemeopts, "h"))      sslopt_ciphers = ciphershigh;
		else if (strstr(httptest->bburl.desturl->schemeopts, "m"))      sslopt_ciphers = ciphersmedium;

		if      (strstr(httptest->bburl.desturl->schemeopts, "10"))     httpversion    = HTTPVER_10;
		else if (strstr(httptest->bburl.desturl->schemeopts, "11"))     httpversion    = HTTPVER_11;
	}

	/* Get any cookies */
	load_cookies();

	/* Generate the request */
	addtobuffer(httprequest, (httptest->bburl.postdata ? "POST " : "GET "));
	switch (httpversion) {
		case HTTPVER_10: 
			addtobuffer(httprequest, (httptest->bburl.proxyurl ? httptest->url : httptest->bburl.desturl->relurl));
			addtobuffer(httprequest, " HTTP/1.0\r\n"); 
			break;

		case HTTPVER_11: 
			/*
			 * Experience shows that even though HTTP/1.1 says you should send the
			 * full URL, some servers (e.g. SunOne App server 7) choke on it.
			 * So just send the good-old relative URL unless we're proxying.
			 */
			addtobuffer(httprequest, (httptest->bburl.proxyurl ? httptest->url : httptest->bburl.desturl->relurl));
			addtobuffer(httprequest, " HTTP/1.1\r\n"); 
			addtobuffer(httprequest, "Connection: close\r\n"); 
			break;
	}

	addtobuffer(httprequest, "Host: ");
	addtobuffer(httprequest, httptest->bburl.desturl->host);
	if ((httptest->bburl.desturl->port != 80) && (httptest->bburl.desturl->port != 443)) {
		char hostporthdr[20];

		sprintf(hostporthdr, ":%d", httptest->bburl.desturl->port);
		addtobuffer(httprequest, hostporthdr);
	}
	addtobuffer(httprequest, "\r\n");

	if (httptest->bburl.postdata) {
		char hdr[100];
		int contlen = strlen(httptest->bburl.postdata);

		if (strncmp(httptest->bburl.postdata, "file:", 5) == 0) {
			/* Load the POST data from a file */
			FILE *pf = fopen(httptest->bburl.postdata+5, "r");
			if (pf == NULL) {
				errprintf("Cannot open POST data file %s\n", httptest->bburl.postdata+5);
				xfree(httptest->bburl.postdata);
				httptest->bburl.postdata = strdup("");
				contlen = 0;
			}
			else {
				struct stat st;

				if (fstat(fileno(pf), &st) == 0) {
					xfree(httptest->bburl.postdata);
					httptest->bburl.postdata = (char *)malloc(st.st_size + 1);
					fread(httptest->bburl.postdata, 1, st.st_size, pf);
					*(httptest->bburl.postdata+st.st_size) = '\0';
					contlen = st.st_size;
				}
				else {
					errprintf("Cannot stat file %s\n", httptest->bburl.postdata+5);
					httptest->bburl.postdata = strdup("");
					contlen = 0;
				}

				fclose(pf);
			}
		}

		addtobuffer(httprequest, "Content-type: ");
		if      (httptest->bburl.postcontenttype) 
			addtobuffer(httprequest, httptest->bburl.postcontenttype);
		else if ((httptest->bburl.testtype == BBTEST_SOAP) || (httptest->bburl.testtype == BBTEST_NOSOAP)) 
			addtobuffer(httprequest, "application/soap+xml; charset=utf-8");
		else 
			addtobuffer(httprequest, "application/x-www-form-urlencoded");
		addtobuffer(httprequest, "\r\n");

		sprintf(hdr, "Content-Length: %d\r\n", contlen);
		addtobuffer(httprequest, hdr);
	}
	{
		char useragent[100];
		void *hinfo;
		char *browser = NULL;

		hinfo = hostinfo(t->host->hostname);
		if (hinfo) browser = bbh_item(hinfo, BBH_BROWSER);

		if (browser) {
			sprintf(useragent, "User-Agent: %s\r\n", browser);
		}
		else {
			sprintf(useragent, "User-Agent: Hobbit bbtest-net/%s\r\n", VERSION);
		}

		addtobuffer(httprequest, useragent);
	}
	if (httptest->bburl.desturl->auth) {
		if (strncmp(httptest->bburl.desturl->auth, "CERT:", 5) == 0) {
			sslopt_clientcert = httptest->bburl.desturl->auth+5;
		}
		else {
			addtobuffer(httprequest, "Authorization: Basic ");
			addtobuffer(httprequest, base64encode(httptest->bburl.desturl->auth));
			addtobuffer(httprequest, "\r\n");
		}
	}
	if (httptest->bburl.proxyurl && httptest->bburl.proxyurl->auth) {
		addtobuffer(httprequest, "Proxy-Authorization: Basic ");
		addtobuffer(httprequest, base64encode(httptest->bburl.proxyurl->auth));
		addtobuffer(httprequest, "\r\n");
	}
	for (ck = cookiehead; (ck); ck = ck->next) {
		int useit = 0;

		if (ck->tailmatch) {
			int startpos = strlen(httptest->bburl.desturl->host) - strlen(ck->host);

			if (startpos > 0) useit = (strcmp(httptest->bburl.desturl->host+startpos, ck->host) == 0);
		}
		else useit = (strcmp(httptest->bburl.desturl->host, ck->host) == 0);
		if (useit) useit = (strncmp(ck->path, httptest->bburl.desturl->relurl, strlen(ck->path)) == 0);

		if (useit) {
			if (firstcookie) {
				addtobuffer(httprequest, "Cookie: ");
				firstcookie = 0;
			}
			addtobuffer(httprequest, ck->name);
			addtobuffer(httprequest, "=");
			addtobuffer(httprequest, ck->value);
			addtobuffer(httprequest, "\r\n");
		}
	}

	/* Some standard stuff */
	addtobuffer(httprequest, "Accept: */*\r\n");
	addtobuffer(httprequest, "Pragma: no-cache\r\n");

	if ((httptest->bburl.testtype == BBTEST_SOAP) || (httptest->bburl.testtype == BBTEST_NOSOAP)) {
		/* Must provide a SOAPAction header */
		addtobuffer(httprequest, "SOAPAction: ");
		addtobuffer(httprequest, httptest->url);
		addtobuffer(httprequest, "\r\n");
	}
	
	/* The final blank line terminates the headers */
	addtobuffer(httprequest, "\r\n");

	/* Post data goes last */
	if (httptest->bburl.postdata) addtobuffer(httprequest, httptest->bburl.postdata);

	/* Pickup any SSL options the user wants */
	if (sslopt_ciphers || (sslopt_version != SSLVERSION_DEFAULT) || sslopt_clientcert){
		sslopt = (ssloptions_t *) malloc(sizeof(ssloptions_t));
		sslopt->cipherlist = sslopt_ciphers;
		sslopt->sslversion = sslopt_version;
		sslopt->clientcert = sslopt_clientcert;
	}

	/* Add to TCP test queue */
	if (httptest->bburl.proxyurl == NULL) {
		httptest->tcptest = add_tcp_test(httptest->bburl.desturl->ip, 
						 httptest->bburl.desturl->port, 
						 httptest->bburl.desturl->scheme,
						 sslopt, NULL,
						 t->testspec, t->silenttest, grabstrbuffer(httprequest), 
						 httptest, tcp_http_data_callback, tcp_http_final_callback);
	}
	else {
		httptest->tcptest = add_tcp_test(httptest->bburl.proxyurl->ip, 
						 httptest->bburl.proxyurl->port, 
						 httptest->bburl.proxyurl->scheme,
						 sslopt, NULL,
						 t->testspec, t->silenttest, grabstrbuffer(httprequest), 
						 httptest, tcp_http_data_callback, tcp_http_final_callback);
	}
}

