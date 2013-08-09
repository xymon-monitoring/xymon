/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libxymon.h"
#include "tcptalk.h"
#include "httpcookies.h"

static char **build_http_dialog(char *testspec, myconn_netparams_t *netparams, void *hostinfo)
{
	char **dialog = NULL;
	strbuffer_t *httprequest;
	weburl_t weburl;
	char *decodedurl;
	enum { HTTPVER_10, HTTPVER_11 } httpversion = HTTPVER_11;
	cookielist_t *ck = NULL;
	int firstcookie = 1;

	/* If there is a parse error in the URL, dont run the test */
	decodedurl = decode_url(testspec, &weburl);
	if (!decodedurl || weburl.desturl->parseerror || (weburl.proxyurl && weburl.proxyurl->parseerror)) {
		freeweburl_data(&weburl);
		return NULL;
	}

	netparams->socktype = CONN_SOCKTYPE_STREAM;
	if (netparams->destinationip) xfree(netparams->destinationip);
	if (weburl.proxyurl)
		netparams->destinationip = strdup(weburl.proxyurl->ip ? weburl.proxyurl->ip : weburl.proxyurl->host);
	else
		netparams->destinationip = strdup(weburl.desturl->ip ? weburl.desturl->ip : weburl.desturl->host);
	netparams->destinationport = (weburl.proxyurl ? weburl.proxyurl->port : weburl.desturl->port);
	netparams->sslhandling = (strcmp(weburl.desturl->scheme, "https") == 0) ? CONN_SSL_YES : CONN_SSL_NO;
	if (netparams->sslname) xfree(netparams->sslname);
	netparams->sslname = strdup(weburl.desturl->host);

#if 0
	/* FIXME: Handle more schemeopts here for SSL versions */
	if      (strstr(httptest->weburl.desturl->schemeopts, "3"))      sslopt_version = SSLVERSION_V3;
	else if (strstr(httptest->weburl.desturl->schemeopts, "2"))      sslopt_version = SSLVERSION_V2;

	if      (strstr(httptest->weburl.desturl->schemeopts, "h"))      sslopt_ciphers = ciphershigh;
	else if (strstr(httptest->weburl.desturl->schemeopts, "m"))      sslopt_ciphers = ciphersmedium;
#endif

	httprequest = newstrbuffer(0);
	addtobuffer(httprequest, "SEND:");

	if (weburl.desturl->schemeopts) {
		if      (strstr(weburl.desturl->schemeopts, "10"))     httpversion    = HTTPVER_10;
		else if (strstr(weburl.desturl->schemeopts, "11"))     httpversion    = HTTPVER_11;
	}

	/* Generate the request */
	addtobuffer(httprequest, (weburl.postdata ? "POST " : "GET "));
	switch (httpversion) {
	  case HTTPVER_10: 
		addtobuffer(httprequest, (weburl.proxyurl ? decodedurl : weburl.desturl->relurl));
		addtobuffer(httprequest, " HTTP/1.0\r\n"); 
		/*
		 * Add a "Connection: close" for HTTP 1.0 - we do not do any
		 * persistent connections.
		 */
		addtobuffer(httprequest, "Connection: close\r\n"); 
		break;

	  case HTTPVER_11: 
		/*
		 * Experience shows that even though HTTP/1.1 says you should send the
		 * full URL, some servers (e.g. SunOne App server 7) choke on it.
		 * So just send the good-old relative URL unless we're proxying.
		 */
		addtobuffer(httprequest, (weburl.proxyurl ? decodedurl : weburl.desturl->relurl));
		addtobuffer(httprequest, " HTTP/1.1\r\n"); 
		/*
		 * addtobuffer(httprequest, "Connection: close\r\n"); 
		 *
		 * Dont add a "Connection: close" header. It is generally not needed
		 * in HTTP 1.1, and in at least one case (google) it causes the
		 * response to use a non-chunked transfer which is much more
		 * difficult to determine when is complete.
		 */
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
		char *browser = NULL;

		if (hostinfo) browser = xmh_item(hostinfo, XMH_BROWSER);

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
			char *s = base64encode(weburl.desturl->auth);

			addtobuffer(httprequest, "Authorization: Basic ");
			addtobuffer(httprequest, s);
			addtobuffer(httprequest, "\r\n");

			xfree(s);
		}
		else {
			/* Client SSL certificate */
			netparams->sslcertfn = strdup(weburl.desturl->auth+5);
		}
	}
	if (weburl.proxyurl && weburl.proxyurl->auth) {
		char *s = base64encode(weburl.proxyurl->auth);

		addtobuffer(httprequest, "Proxy-Authorization: Basic ");
		addtobuffer(httprequest, s);
		addtobuffer(httprequest, "\r\n");

		xfree(s);
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
	switch (httpversion) {
	  case HTTPVER_10: 
		addtobuffer(httprequest, "Pragma: no-cache\r\n");
		break;
	  case HTTPVER_11: 
		addtobuffer(httprequest, "Cache-control: no-cache\r\n");
		break;
	}

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
	dialog = (char **)calloc(4, sizeof(char *));
	dialog[0] = grabstrbuffer(httprequest);
	dialog[1] = strdup("READALL");
	dialog[2] = strdup("CLOSE");
	dialog[3] = NULL;

	freeweburl_data(&weburl);

	return dialog;
}


static char **build_standard_dialog(char *testspec, myconn_netparams_t *netparams, net_test_options_t *options, void *hostinfo)
{
	char *silentopt, *portopt;
	int port = 0, silenttest = 0;
	netdialog_t *rec = NULL;
	xtreePos_t handle;

	silentopt = strrchr(testspec, ':');
	if (silentopt) {
		/* Cut off the "silent" modifier */
		if ((strcasecmp(silentopt, ":s") == 0) || (strcasecmp(silentopt, ":q") == 0)) {
			silenttest = 1;
			*silentopt = '\0';
		}
		else
			silentopt = NULL;
	}

	portopt = strrchr(testspec, ':');
	if (portopt) {
		port = atoi(portopt);
		if (port > 0) {
			*portopt = '\0';
		}
		else {
			port = 0;
			portopt = NULL;
		}
	}

	/* We special-case the rpc check here, because it uses "rpc=..." syntax */
	if (argnmatch(testspec, "rpc=")) {
		rec = find_net_service("rpc");
	}
	else {
		rec = find_net_service(testspec);
	}

	/* Must restore the stuff we have cut off with silent/portnumber options - it might not be a net test at all! */
	if (portopt) *portopt = ':';
	if (silentopt) *silentopt = ':';

	if (rec) {
		if (port)
			netparams->destinationport = port;
		else
			netparams->destinationport = rec->portnumber;

		netparams->socktype = (rec->option_udp ? CONN_SOCKTYPE_DGRAM : CONN_SOCKTYPE_STREAM);

		if (rec->option_ssl) netparams->sslhandling = CONN_SSL_YES;
		else if (rec->option_starttls) netparams->sslhandling = CONN_SSL_STARTTLS_CLIENT;
		else netparams->sslhandling = CONN_SSL_NO;

		if (rec->option_telnet) options->testtype = NET_TEST_TELNET;
		else if (rec->option_dns) options->testtype = NET_TEST_DNS;
		else if (rec->option_ntp) options->testtype = NET_TEST_NTP;
		else if (rec->option_external) options->testtype = NET_TEST_EXTERNAL;
		else options->testtype = NET_TEST_STANDARD;

		return (silenttest ? silentdialog : rec->dialog);
	}

	return NULL;
}


static char **build_ldap_dialog(char *testspec, myconn_netparams_t *netparams, net_test_options_t *options, void *hostinfo)
{
	char *decodedurl;
	char **dialog;
	weburl_t weburl;

	/* If there is a parse error in the URL, dont run the test */
	decodedurl = decode_url(testspec, &weburl);
	if (!decodedurl || weburl.desturl->parseerror) {
		freeweburl_data(&weburl);
		return NULL;
	}

	dialog = build_standard_dialog(weburl.desturl->scheme, netparams, options, hostinfo);
	/* Use the destination in the URL, rather than the one provided for the host */
	if (netparams->destinationip) xfree(netparams->destinationip);
	netparams->destinationip = strdup(weburl.desturl->ip ? weburl.desturl->ip : weburl.desturl->host);

	freeweburl_data(&weburl);
	return dialog;
}


char **net_dialog(char *testspec, myconn_netparams_t *netparams, net_test_options_t *options, void *hostinfo, int *dtoken)
{
	char **result = NULL;

	/* Skip old-style modifiers */
	testspec += strspn(testspec, "?!~");

	if (argnmatch(testspec, "ldap://") || argnmatch(testspec, "ldaps://") ||argnmatch(testspec, "ldaptls://")) {
		result = build_ldap_dialog(testspec, netparams, options, hostinfo);
		options->testtype = NET_TEST_LDAP;
	}
	else if ( argnmatch(testspec, "http")         ||
		  argnmatch(testspec, "content=http") ||
		  argnmatch(testspec, "cont;http")    ||
		  argnmatch(testspec, "cont=")        ||
		  argnmatch(testspec, "nocont;http")  ||
		  argnmatch(testspec, "nocont=")      ||
		  argnmatch(testspec, "post;http")    ||
		  argnmatch(testspec, "post=")        ||
		  argnmatch(testspec, "nopost;http")  ||
		  argnmatch(testspec, "nopost=")      ||
		  argnmatch(testspec, "soap;http")    ||
		  argnmatch(testspec, "soap=")        ||
		  argnmatch(testspec, "nosoap;http")  ||
		  argnmatch(testspec, "nosoap=")      ||
		  argnmatch(testspec, "type;http")    ||
		  argnmatch(testspec, "type=")        )      {
		result = build_http_dialog(testspec, netparams, hostinfo);
		options->testtype = NET_TEST_HTTP;
	}
	else if (argnmatch(testspec, "apache") || argnmatch(testspec, "apache=")) {
		char *url = strchr(testspec, '=');

		if (url) 
			url = strdup(url+1);
		else {
			char *target = xmh_item(hostinfo, XMH_HOSTNAME);

			url = (char *)malloc(30 + strlen(target));
			sprintf(url, "http://%s/server-status?auto", target);
		}

		result = build_http_dialog(url, netparams, hostinfo);
		options->testtype = NET_TEST_HTTP;
		xfree(url);
	}
	else {
		/* Default to NET_TEST_STANDARD, but build_standard_dialog() may override it */
		options->testtype = NET_TEST_STANDARD;
		result = build_standard_dialog(testspec, netparams, options, hostinfo);
	}

	*dtoken = options->testtype;
	return result;
}


void free_net_dialog(char **dialog, int dtoken)
{
	int i;

	if (!dialog) return;

	switch (dtoken) {
	  case NET_TEST_HTTP:
		for (i=0; (dialog[i]); i++) xfree(dialog[i]);
		xfree(dialog);
		break;

	  default:
		break;
	}
}

