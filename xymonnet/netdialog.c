/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2012 Henrik Storner <henrik@hswn.dk>                    */
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
#include "tcptalk.h"
#include "httpcookies.h"

typedef struct netdialog_t {
	char *name;
	int portnumber;
	char **dialog;
	int option_telnet:1;
	int option_ntp:1;
	int option_dns:1;
	int option_ssl:1;
	int option_starttls:1;
	int option_udp:1;
} netdialog_t;

static void *netdialogs = NULL;

void load_protocols(char *fn)
{
	static void *cfgfilelist = NULL;
	char *configfn;
	FILE *fd;
	strbuffer_t *l;
	netdialog_t *rec = NULL;
	int dialogsz = 0;

	if (!fn) {
		configfn = (char *)malloc(strlen(xgetenv("XYMONHOME")) + strlen("/etc/protocols.cfg") + 1);
		sprintf(configfn, "%s/etc/protocols.cfg", xgetenv("XYMONHOME"));
	}
	else
		configfn = strdup(fn);

	if (cfgfilelist && !stackfmodified(cfgfilelist)) {
		dbgprintf("protocols.cfg unchanged, skipping reload\n");
		xfree(configfn);
		return;
	}

	fd = stackfopen(configfn, "r", &cfgfilelist);
	if (!fd) {
		errprintf("Cannot open protocols.cfg file %s\n", configfn);
		xfree(configfn);
		return;
	}

	xfree(configfn);

	/* Wipe out the current configuration */
	if (netdialogs) {
		xtreePos_t handle = xtreeFirst(netdialogs);
		while (handle != xtreeEnd(netdialogs)) {
			int i;
			netdialog_t *rec = xtreeData(netdialogs, handle);
			handle = xtreeNext(netdialogs, handle);

			if (rec->name) xfree(rec->name);
			if (rec->dialog) {
				for (i=0; (rec->dialog[i]); i++) xfree(rec->dialog[i]);
				xfree(rec->dialog);
			}
			xfree(rec);
		}
		xtreeDestroy(netdialogs);
		netdialogs = NULL;
	}

	netdialogs = xtreeNew(strcmp);
	l = newstrbuffer(0);
	while (stackfgets(l, NULL)) {
		char *p;

		sanitize_input(l, 1, 0);
		if (STRBUFLEN(l) == 0) continue;

		if (*STRBUF(l) == '[') {
			rec = (netdialog_t *)calloc(1, sizeof(netdialog_t));
			rec->name = strdup(STRBUF(l)+1);
			p = strchr(rec->name, ']'); if (p) *p = '\0';
			xtreeAdd(netdialogs, rec->name, rec);
			rec->portnumber = conn_lookup_portnumber(rec->name, 0);
			dialogsz = 0;
		}
		else if (strncasecmp(STRBUF(l), "port ", 5) == 0) {
			rec->portnumber = atoi(STRBUF(l)+5);
		}
		else if (strncasecmp(STRBUF(l), "options ", 8) == 0) {
			char *tok, *savptr;

			tok = strtok_r(STRBUF(l), " \t", &savptr);
			tok = strtok_r(NULL, ",", &savptr);
			while (tok) {
				if (strcasecmp(tok, "telnet") == 0) rec->option_telnet = 1;
				if (strcasecmp(tok, "ntp") == 0) rec->option_ntp = 1;
				if (strcasecmp(tok, "dns") == 0) rec->option_dns = 1;
				if (strcasecmp(tok, "ssl") == 0) rec->option_ssl = 1;
				if (strcasecmp(tok, "starttls") == 0) rec->option_starttls = 1;
				if (strcasecmp(tok, "udp") == 0) rec->option_udp = 1;
				tok = strtok_r(NULL, ",", &savptr);
			}
		}
		else if ( (strncasecmp(STRBUF(l), "send:", 5) == 0) ||
			  (strncasecmp(STRBUF(l), "expect:", 7) == 0) ||
			  (strncasecmp(STRBUF(l), "read", 4) == 0) ||
			  (strncasecmp(STRBUF(l), "close", 5) == 0) ||
			  (strncasecmp(STRBUF(l), "starttls", 8) == 0) ) {
			dialogsz++;
			rec->dialog = (char **)realloc(rec->dialog, (dialogsz+1)*sizeof(char *));
			rec->dialog[dialogsz-1] = strdup(STRBUF(l));
			rec->dialog[dialogsz] = NULL;
		}
	}

	freestrbuffer(l);
	stackfclose(fd);
}

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
	netparams->destinationport = (weburl.proxyurl ? weburl.proxyurl->port : weburl.desturl->port);
	netparams->sslhandling = (strcmp(weburl.desturl->scheme, "https") == 0) ? CONN_SSL_YES : CONN_SSL_NO;

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
			addtobuffer(httprequest, "Authorization: Basic ");
			addtobuffer(httprequest, base64encode(weburl.desturl->auth));
			addtobuffer(httprequest, "\r\n");
		}
		else {
			/* Client SSL certificate */
			netparams->sslcertfn = strdup(weburl.desturl->auth+5);
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

char **net_dialog(char *testspec, myconn_netparams_t *netparams, enum net_test_options_t *options, void *hostinfo)
{
	int dialuptest = 0, reversetest = 0, alwaystruetest = 0, silenttest = 0;

	/* Skip old-style modifiers */
	if (*testspec == '?') { dialuptest=1;     testspec++; }
	if (*testspec == '!') { reversetest=1;    testspec++; }
	if (*testspec == '~') { alwaystruetest=1; testspec++; }

	if ((strcmp(testspec, "http") == 0) || (strcmp(testspec, "https") == 0)) {
		errprintf("Host %s: http/https tests requires a full URL\n", xmh_item(hostinfo, XMH_HOSTNAME));
		return NULL;
	}
	else if ((argnmatch(testspec, "ldap://")) || (argnmatch(testspec, "ldaps://"))) {
		/* LDAP test - handled in another module */
		return NULL;
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

		*options = NET_TEST_HTTP;
		return  build_http_dialog(testspec, netparams, hostinfo);
	}
	else {
		xtreePos_t handle;
		char *opt, *port = NULL;

		opt = strrchr(testspec, ':');
		if (opt && ((strcasecmp(opt, ":s") == 0) || (strcasecmp(opt, ":q") == 0))) {
			silenttest = 1;
			*opt = '\0';
			port = strrchr(testspec, ':');
			*opt = ':';
		}
		else if (opt)
			port = opt;

		if (port) *port = '\0';
		handle = xtreeFind(netdialogs, testspec);
		if (port) *port = ':';

		if (handle != xtreeEnd(netdialogs)) {
			netdialog_t *rec = xtreeData(netdialogs, handle);

			if (port)
				netparams->destinationport = atoi(port+1);
			else
				netparams->destinationport = rec->portnumber;

			netparams->socktype = (rec->option_udp ? CONN_SOCKTYPE_DGRAM : CONN_SOCKTYPE_STREAM);

			if (rec->option_ssl) netparams->sslhandling = CONN_SSL_YES;
			else if (rec->option_starttls) netparams->sslhandling = CONN_SSL_STARTTLS_CLIENT;
			else netparams->sslhandling = CONN_SSL_NO;

			if (rec->option_telnet) *options = NET_TEST_TELNET;
			if (rec->option_dns) *options = NET_TEST_DNS;
			if (rec->option_ntp) *options = NET_TEST_NTP;
			else *options = NET_TEST_STANDARD;

			return rec->dialog;
		}
	}

	return NULL;
}

#if 0
			service_t *s = NULL;
			int dialuptest = 0, reversetest = 0, silenttest = 0, sendasdata = 0;
			char *srcip = NULL;
			int alwaystruetest = (xmh_item(hwalk, XMH_FLAG_NOCLEAR) != NULL);

			if (xmh_item_idx(testspec) == -1) {

				/* Test prefixes:
				 * - '?' denotes dialup test, i.e. report failures as clear.
				 * - '|' denotes reverse test, i.e. service should be DOWN.
				 * - '~' denotes test that ignores ping result (normally,
				 *       TCP tests are reported CLEAR if ping check fails;
				 *       with this flag report their true status)
				 */
				if (*testspec == '?') { dialuptest=1;     testspec++; }
				if (*testspec == '!') { reversetest=1;    testspec++; }
				if (*testspec == '~') { alwaystruetest=1; testspec++; }

				if (pingtest && argnmatch(testspec, pingtest->testname)) {
					char *p;

					/*
					 * Ping/conn test. Save any modifier flags for later use.
					 */
					ping_dialuptest = dialuptest;
					ping_reversetest = reversetest;
					p = strchr(testspec, '=');
					if (p) {
						char *ips;

						/* Extra ping tests - save them for later */
						h->extrapings = (extraping_t *)malloc(sizeof(extraping_t));
						h->extrapings->iplist = NULL;
						if (argnmatch(p, "=worst,")) {
							h->extrapings->matchtype = MULTIPING_WORST;
							ips = strdup(p+7);
						}
						else if (argnmatch(p, "=best,")) {
							h->extrapings->matchtype = MULTIPING_BEST;
							ips = strdup(p+6);
						}
						else {
							h->extrapings->matchtype = MULTIPING_BEST;
							ips = strdup(p+1);
						}

						do {
							ipping_t *newping = (ipping_t *)malloc(sizeof(ipping_t));

							newping->ip = ips;
							newping->open = 0;
							newping->banner = newstrbuffer(0);
							newping->next = h->extrapings->iplist;
							h->extrapings->iplist = newping;
							ips = strchr(ips, ',');
							if (ips) { *ips = '\0'; ips++; }
						} while (ips && (*ips));
					}
					s = NULL; /* Dont add the test now - ping is special (enabled by default) */
				}
				else if ((argnmatch(testspec, "ldap://")) || (argnmatch(testspec, "ldaps://"))) {
					/*
					 * LDAP test. This uses ':' a lot, so save it here.
					 */
#ifdef HAVE_LDAP
					s = ldaptest;
					add_url_to_dns_queue(testspec);
#else
					errprintf("Host %s: ldap test requested, but xymonnet was built with no ldap support\n", xmh_item(hwalk, XMH_HOSTNAME));
#endif
				}
				else if ((strcmp(testspec, "http") == 0) || (strcmp(testspec, "https") == 0)) {
					errprintf("Host %s: http/https tests requires a full URL\n", xmh_item(hwalk, XMH_HOSTNAME));
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
					  argnmatch(testspec, "nosoap;http")    ||
					  argnmatch(testspec, "nosoap=")        ||
					  argnmatch(testspec, "type;http")    ||
					  argnmatch(testspec, "type=")        )      {

					/* HTTP test. */
					weburl_t url;

					decode_url(testspec, &url);
					if (url.desturl->parseerror || (url.proxyurl && url.proxyurl->parseerror)) {
						s = NULL;
						errprintf("Host %s: Invalid URL for http test - ignored: %s\n", 
							  xmh_item(hwalk, XMH_HOSTNAME), testspec);
					}
					else {
						s = httptest;
						if (!url.desturl->ip)
							add_url_to_dns_queue(testspec);
					}
				}
				else if (argnmatch(testspec, "apache") || argnmatch(testspec, "apache=")) {
					char *userfmt = "cont=apache;%s;.";
					char *deffmt = "cont=apache;http://%s/server-status?auto;.";
					static char *statusurl = NULL;
					char *userurl;

					if (statusurl != NULL) xfree(statusurl);

					userurl = strchr(testspec, '='); 
					if (userurl) {
						weburl_t url;
						userurl++;

						decode_url(userurl, &url);
						if (url.desturl->parseerror || (url.proxyurl && url.proxyurl->parseerror)) {
							s = NULL;
							errprintf("Host %s: Invalid URL for apache test - ignored: %s\n", xmh_item(hwalk, XMH_HOSTNAME), testspec);
						}
						else {
							statusurl = (char *)malloc(strlen(userurl) + strlen(userfmt) + 1);
							sprintf(statusurl, userfmt, userurl);
							s = httptest;
						}
					}
					else {
						char *ip = xmh_item(hwalk, XMH_IP);
						statusurl = (char *)malloc(strlen(deffmt) + strlen(ip) + 1);
						sprintf(statusurl, deffmt, ip);
						s = httptest;
					}

					if (s) {
						testspec = statusurl;
						add_url_to_dns_queue(testspec);
						sendasdata = 1;
					}
				}
				else if (argnmatch(testspec, "rpc")) {
					/*
					 * rpc check via rpcinfo
					 */
					s = rpctest;
				}
				else if (argnmatch(testspec, "dns=")) {
					s = dnstest;
				}
				else if (argnmatch(testspec, "dig=")) {
					s = dnstest;
				}
				else {
					/* 
					 * Simple TCP connect test. 
					 */
					char *option;
					xtreePos_t handle;

					/* See if there's a source IP */
					srcip = strchr(testspec, '@');
					if (srcip) {
						*srcip = '\0';
						srcip++;
					}

					/* Remove any trailing ":s", ":q", ":Q", ":portnumber" */
					option = strchr(testspec, ':'); 
					if (option) { 
						*option = '\0'; 
						option++; 
					}
	
					/* Find the service */
					handle = xtreeFind(svctree, testspec);
					s = ((handle == xtreeEnd(svctree)) ? NULL : (service_t *)xtreeData(svctree, handle));
					if (option && s) {
						/*
						 * Check if it is a service with an explicit portnumber.
						 * If it is, then create a new service record named
						 * "SERVICE_PORT" so we can merge tests for this service+port
						 * combination for multiple hosts.
						 *
						 * According to Xymon docs, this type of services must be in
						 * XYMONNETSVCS - so it is known already.
						 */
						int specialport = 0;
						char *specialname;
						char *opt2 = strrchr(option, ':');

						if (opt2) {
							if (strcmp(opt2, ":s") == 0) {
								/* option = "portnumber:s" */
								silenttest = 1;
								*opt2 = '\0';
								specialport = atoi(option);
								*opt2 = ':';
							}
						}
						else if (strcmp(option, "s") == 0) {
							/* option = "s" */
							silenttest = 1;
							specialport = 0;
						}
						else {
							/* option = "portnumber" */
							specialport = atoi(option);
						}

						if (specialport) {
							specialname = (char *) malloc(strlen(s->testname)+10);
							sprintf(specialname, "%s_%d", s->testname, specialport);
							s = add_service(specialname, specialport, strlen(s->testname), TOOL_CONTEST);
							xfree(specialname);
						}
					}

					if (s) h->dodns = 1;
					if (option) *(option-1) = ':';
				}

				if (s) {
					testitem_t *newtest;

					anytests = 1;
					newtest = init_testitem(h, s, srcip, testspec, dialuptest, reversetest, alwaystruetest, silenttest, sendasdata);
					newtest->next = s->items;
					s->items = newtest;

					if (s == httptest) h->firsthttp = newtest;
					else if (s == ldaptest) {
						xtreePos_t handle;
						service_t *s2 = NULL;
						testitem_t *newtest2;

						h->firstldap = newtest;

						/* 
						 * Add a plain tcp-connect test for the LDAP service.
						 * We don't want the LDAP library to run amok and do 
						 * time-consuming connect retries if the service
						 * is down.
						 */
						handle = xtreeFind(svctree, "ldap");
						s2 = ((handle == xtreeEnd(svctree)) ? NULL : (service_t *)xtreeData(svctree, handle));
						if (s2) {
							newtest2 = init_testitem(h, s2, NULL, "ldap", 0, 0, 0, 0, 1);
							newtest2->internal = 1;
							newtest2->next = s2->items;
							s2->items = newtest2;
							newtest->privdata = newtest2;
						}
					}
				}
			}
#endif

