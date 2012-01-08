/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns2.c 6743 2011-09-03 15:44:52Z storner $";

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libxymon.h"
#include "httpcookies.h"
#include "tcphttp.h"

char **build_http_dialog(char *testspec)
{
	char **dialog = NULL;
	strbuffer_t *httprequest;
	weburl_t weburl;
	char *decodedurl;
	int  httpversion = HTTPVER_11;
	cookielist_t *ck = NULL;
	int firstcookie = 1;

	/* If there is a parse error in the URL, dont run the test */
	decodedurl = decode_url(testspec, &weburl);
	if (!decodedurl) return NULL;
	if ((weburl.proxyurl && weburl.proxyurl->parseerror) || (!weburl.proxyurl && weburl.desturl->parseerror)) return NULL;

	httprequest = newstrbuffer(0);
	addtobuffer(httprequest, "SEND:");

	if (weburl.desturl->schemeopts) {
		if      (strstr(weburl.desturl->schemeopts, "10"))     httpversion    = HTTPVER_10;
		else if (strstr(weburl.desturl->schemeopts, "11"))     httpversion    = HTTPVER_11;
	}

	/* Get any cookies */
	load_cookies();

	/* Generate the request */
	addtobuffer(httprequest, (weburl.postdata ? "POST " : "GET "));
	switch (httpversion) {
		case HTTPVER_10: 
			addtobuffer(httprequest, (weburl.proxyurl ? decodedurl : weburl.desturl->relurl));
			addtobuffer(httprequest, " HTTP/1.0\r\n"); 
			break;

		case HTTPVER_11: 
			/*
			 * Experience shows that even though HTTP/1.1 says you should send the
			 * full URL, some servers (e.g. SunOne App server 7) choke on it.
			 * So just send the good-old relative URL unless we're proxying.
			 */
			addtobuffer(httprequest, (weburl.proxyurl ? decodedurl : weburl.desturl->relurl));
			addtobuffer(httprequest, " HTTP/1.1\r\n"); 
			// addtobuffer(httprequest, "Connection: close\r\n"); 
			break;
	}

	addtobuffer(httprequest, "Host: ");
	addtobuffer(httprequest, weburl.desturl->host);
	if ((weburl.desturl->port != 80) && (weburl.desturl->port != 443)) {
		char hostporthdr[20];

		sprintf(hostporthdr, ":%d", weburl.desturl->port);
		addtobuffer(httprequest, hostporthdr);
	}
	addtobuffer(httprequest, "\r\n");

	if (weburl.postdata) {
		char hdr[100];
		int contlen = strlen(weburl.postdata);

		if (strncmp(weburl.postdata, "file:", 5) == 0) {
			/* Load the POST data from a file */
			FILE *pf = fopen(weburl.postdata+5, "r");
			if (pf == NULL) {
				errprintf("Cannot open POST data file %s\n", weburl.postdata+5);
				xfree(weburl.postdata);
				weburl.postdata = strdup("");
				contlen = 0;
			}
			else {
				struct stat st;

				if (fstat(fileno(pf), &st) == 0) {
					xfree(weburl.postdata);
					weburl.postdata = (char *)malloc(st.st_size + 1);
					fread(weburl.postdata, 1, st.st_size, pf);
					*(weburl.postdata+st.st_size) = '\0';
					contlen = st.st_size;
				}
				else {
					errprintf("Cannot stat file %s\n", weburl.postdata+5);
					weburl.postdata = strdup("");
					contlen = 0;
				}

				fclose(pf);
			}
		}

		addtobuffer(httprequest, "Content-type: ");
		if      (weburl.postcontenttype) 
			addtobuffer(httprequest, weburl.postcontenttype);
		else if ((weburl.testtype == WEBTEST_SOAP) || (weburl.testtype == WEBTEST_NOSOAP)) 
			addtobuffer(httprequest, "application/soap+xml; charset=utf-8");
		else 
			addtobuffer(httprequest, "application/x-www-form-urlencoded");
		addtobuffer(httprequest, "\r\n");

		sprintf(hdr, "Content-Length: %d\r\n", contlen);
		addtobuffer(httprequest, hdr);
	}
	{
		char useragent[100];
		void *hinfo = NULL;
		char *browser = NULL;

//		hinfo = hostinfo(t->host->hostname);
//		if (hinfo) browser = xmh_item(hinfo, XMH_BROWSER);

		if (browser) {
			sprintf(useragent, "User-Agent: %s\r\n", browser);
		}
		else {
			sprintf(useragent, "User-Agent: Xymon xymonnet/%s\r\n", VERSION);
		}

		addtobuffer(httprequest, useragent);
	}
	if (weburl.desturl->auth) {
		if (strncmp(weburl.desturl->auth, "CERT:", 5) != 0) {
			addtobuffer(httprequest, "Authorization: Basic ");
			addtobuffer(httprequest, base64encode(weburl.desturl->auth));
			addtobuffer(httprequest, "\r\n");
		}
	}
	if (weburl.proxyurl && weburl.proxyurl->auth) {
		addtobuffer(httprequest, "Proxy-Authorization: Basic ");
		addtobuffer(httprequest, base64encode(weburl.proxyurl->auth));
		addtobuffer(httprequest, "\r\n");
	}
	for (ck = cookiehead; (ck); ck = ck->next) {
		int useit = 0;

		if (ck->tailmatch) {
			int startpos = strlen(weburl.desturl->host) - strlen(ck->host);

			if (startpos > 0) useit = (strcmp(weburl.desturl->host+startpos, ck->host) == 0);
		}
		else useit = (strcmp(weburl.desturl->host, ck->host) == 0);
		if (useit) useit = (strncmp(ck->path, weburl.desturl->relurl, strlen(ck->path)) == 0);

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

	if ((weburl.testtype == WEBTEST_SOAP) || (weburl.testtype == WEBTEST_NOSOAP)) {
		/* Must provide a SOAPAction header */
		addtobuffer(httprequest, "SOAPAction: ");
		addtobuffer(httprequest, decodedurl);
		addtobuffer(httprequest, "\r\n");
	}
	
	/* The final blank line terminates the headers */
	addtobuffer(httprequest, "\r\n");

	/* Post data goes last */
	if (weburl.postdata) addtobuffer(httprequest, weburl.postdata);

	/* All done, build the dialog for simply sending the request and reading back the response */
	dialog = (char **)calloc(3, sizeof(char *));
	dialog[0] = grabstrbuffer(httprequest);
	dialog[1] = "READALL";
	dialog[2] = NULL;

	freeweburl_data(&weburl);

	return dialog;
}

