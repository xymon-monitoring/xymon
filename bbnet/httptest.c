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

static char rcsid[] = "$Id: httptest.c,v 1.46 2003-08-29 08:26:25 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>

#include "bbgen.h"
#include "util.h"
#include "sendmsg.h"
#include "debug.h"
#include "bbtest-net.h"
#include "httptest.h"

char *http_library_version = NULL;

static int can_ssl = 1;
static int can_ldap = 0;
static FILE *logfd = NULL;

int init_http_library(void)
{
	if (curl_global_init(CURL_GLOBAL_DEFAULT)) {
		errprintf("FATAL: Cannot initialize libcurl!\n");
		return 1;
	}

	http_library_version = malcop(curl_version());

#if (LIBCURL_VERSION_NUM >= 0x070a00)
	{
		curl_version_info_data *curlver;
		int i;

		/* Check libcurl version */
		curlver = curl_version_info(CURLVERSION_NOW);
		if (curlver->age != CURLVERSION_NOW) {
			errprintf("Unknown libcurl version - please recompile bbtest-net\n");
			return 1;
		}

		if (curlver->ssl_version_num == 0) {
			errprintf("WARNING: No SSL support in libcurl - https tests disabled\n");
			can_ssl = 0;
		}

		if (curlver->version_num < LIBCURL_VERSION_NUM) {
			errprintf("WARNING: Compiled against libcurl %s, but running on version %s\n",
				LIBCURL_VERSION, curlver->version);
		}

		for (i=0; (curlver->protocols[i]); i++) {
			dprintf("Curl supports %s\n", curlver->protocols[i]);
			if (strcmp(curlver->protocols[i], "ldap") == 0) can_ldap = 1;
		}
	}
#else

/*
 * Many systems (e.g. Debian) have version 7.9.5, so try to work
 * with what we have but give a warning about some features not
 * being supported.
 */
#warning libcurl older than 7.10.x is not supported - trying to build anyway
#warning SSL certificate checks will NOT work.
#warning Use of the ~/.netrc for usernames and passwords will NOT work.

#if (LIBCURL_VERSION_NUM < 0x070907)
#define CURLOPT_WRITEDATA CURLOPT_FILE
#endif

#endif

	return 0;
}

void shutdown_http_library(void)
{
	curl_global_cleanup();
}


void add_http_test(testitem_t *t)
{
	/* See http://www.openssl.org/docs/apps/ciphers.html for cipher strings */
	static char *ciphersmedium = "MEDIUM";	/* Must be formatted for openssl library */
	static char *ciphershigh = "HIGH";	/* Must be formatted for openssl library */

	http_data_t *req;
	char *proto = NULL;
	char *proxy = NULL;
	char *proxyuserpwd = NULL;
	char *ip = NULL;
	char *hosthdr = NULL;
	int status;

	/* 
	 * t->testspec containts the full testspec
	 * It can be either "URL", "content=URL", 
	 * "cont;URL;expected_data", "post;URL;postdata;expected_data"
	 */

	/* Allocate the private data and initialize it */
	req = (http_data_t *) malloc(sizeof(http_data_t));
	t->privdata = (void *) req;
	req->url = malcop(realurl(t->testspec, &proxy, &proxyuserpwd, &ip, &hosthdr));

	if (proxy) {
		req->proxy = malcop(proxy); 
		req->proxyuserpwd = malcop(proxyuserpwd);
	}
	else req->proxy = req->proxyuserpwd = NULL;

	if (ip) req->ip = malcop(ip); else req->ip = NULL;
	if (hosthdr) req->hosthdr = malcop(hosthdr); else req->hosthdr = NULL;
	req->postdata = NULL;
	req->sslversion = 0;
	req->ciphers = NULL;
	req->curl = NULL;
	req->slist = NULL;
	req->res = CURL_LAST;
	req->errorbuffer[0] = '\0';
	req->httpstatus = 0;
	req->contstatus = 0;
	req->headers = NULL;
	req->contenttype = NULL;
	req->output = NULL;
	req->httpcolor = -1;
	req->faileddeps = NULL;
	req->sslcolor = -1;
	req->logcert = 0;
	req->certinfo = NULL;
	req->certexpires = 0;
	req->totaltime = 0.0;
	req->exp = NULL;

	if ((strncmp(req->url, "https", 5) == 0) && !can_ssl) {
		/* Cannot check HTTPS without a working SSL-enabled curl */
		strcpy(req->errorbuffer, "Run-time libcurl does not support SSL tests");
		return;
	}

	/* Determine the content data to look for (if any) */
	if (strncmp(t->testspec, "content=", 8) == 0) {
		FILE *contentfd;
		char contentfn[200];
		char l[MAX_LINE_LEN];
		char *p;

		sprintf(contentfn, "%s/content/%s.substring", getenv("BBHOME"), commafy(t->host->hostname));
		contentfd = fopen(contentfn, "r");
		if (contentfd) {
			if (fgets(l, sizeof(l), contentfd)) {
				p = strchr(l, '\n'); if (p) { *p = '\0'; };
				req->exp = (regex_t *) malloc(sizeof(regex_t));
				status = regcomp(req->exp, l, REG_EXTENDED|REG_NOSUB);
				if (status) {
					errprintf("Failed to compile regexp '%s' for URL %s\n", p, req->url);
					req->contstatus = STATUS_CONTENTMATCH_BADREGEX;
				}
			}
			else {
				req->contstatus = STATUS_CONTENTMATCH_NOFILE;
			}
			fclose(contentfd);
		}
		else {
			req->contstatus = STATUS_CONTENTMATCH_NOFILE;
		}
		proto = t->testspec + 8;
	}
	else if (strncmp(t->testspec, "cont;", 5) == 0) {
		char *p = strrchr(t->testspec, ';');
		if (p) {
			req->exp = (regex_t *) malloc(sizeof(regex_t));
			status = regcomp(req->exp, p+1, REG_EXTENDED|REG_NOSUB);
			if (status) {
				errprintf("Failed to compile regexp '%s' for URL %s\n", p+1, req->url);
				req->contstatus = STATUS_CONTENTMATCH_BADREGEX;
			}
		}
		else req->contstatus = STATUS_CONTENTMATCH_NOFILE;
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
				req->exp = (regex_t *) malloc(sizeof(regex_t));
				status = regcomp(req->exp, p+1, REG_EXTENDED|REG_NOSUB);
				if (status) {
					errprintf("Failed to compile regexp '%s' for URL %s\n", p+1, req->url);
					req->contstatus = STATUS_CONTENTMATCH_BADREGEX;
				}
			}
		}
		else req->contstatus = STATUS_CONTENTMATCH_NOFILE;

		if (p) {
			*p = '\0';  /* Cut off expected data */
			q = strrchr(t->testspec, ';');
			if (q) req->postdata = malcop(q+1);
			*p = ';';  /* Restore testspec */
		}

		proto = t->testspec+5;
	}
	else {
		proto = t->testspec;
	}

	if      (strncmp(proto, "https3:", 7) == 0)      req->sslversion = 3;
	else if (strncmp(proto, "https2:", 7) == 0)      req->sslversion = 2;
	else if (strncmp(proto, "httpsh:", 7) == 0)      req->ciphers = ciphershigh;
	else if (strncmp(proto, "httpsm:", 7) == 0)      req->ciphers = ciphersmedium;
}



static int statuscolor(testedhost_t *h, long status)
{
	int result;

	switch(status) {
	  case 000:			/* curl reports error */
		result = (h->dialup ? COL_CLEAR : COL_RED);
		break;
	  case 200:
	  case 301:
	  case 302:
	  case 401:
	  case 403:			/* Is "Forbidden" an OK status ? */
		result = COL_GREEN;
		break;
	  case 400:
	  case 404:
		result = COL_RED;	/* Trouble getting page */
		break;
	  case 500:
	  case 501:
	  case 502:  /* Proxy error */
	  case 503:
		result = COL_RED;	/* Server error */
		break;
	  case STATUS_CONTENTMATCH_FAILED:
		result = COL_RED;		/* Pseudo status: content match fails */
		break;
	  case STATUS_CONTENTMATCH_BADREGEX:	/* Pseudo status: bad regex to match against */
	  case STATUS_CONTENTMATCH_NOFILE:	/* Pseudo status: content match requested, but no match-file */
		result = COL_YELLOW;
		break;
	  default:
		result = COL_YELLOW;	/* Unknown status */
		break;
	}

	/* Drop failures if not inside SLA window */
	if ((result >= COL_YELLOW) && (!h->okexpected)) {
		result = COL_BLUE;
	}

	return result;
}


static size_t hdr_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
	/* 
	 * Gets called with all header lines, one line at a time 
	 * "stream" points to the http_data_t record.
	 */

	http_data_t *req = stream;
	size_t count = size*nmemb;

	if (logfd) fprintf(logfd, "%s", (char *)ptr);

	if (req->headers == NULL) {
		req->headers = (char *) malloc(count+1);
		memcpy(req->headers, ptr, count);
		*(req->headers+count) = '\0';
	}
	else {
		size_t buflen = strlen(req->headers);
		req->headers = (char *) realloc(req->headers, buflen+count+1);
		memcpy(req->headers+buflen, ptr, count);
		*(req->headers+buflen+count) = '\0';
	}

	return count;
}


static size_t data_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
	/* 
	 * Gets called with page data from the webserver.
	 * "stream" points to the http_data_t record.
	 */

	http_data_t *req = stream;
	size_t count = size*nmemb;

	if (logfd) fprintf(logfd, "%s", (char *)ptr);

	if (req->exp == NULL) {
		/* No need to save output - just drop it */
		return count;
	}

	if (req->output == NULL) {
		req->output = (char *) malloc(count+1);
		memcpy(req->output, ptr, count);
		*(req->output+count) = '\0';
	}
	else {
		size_t buflen = strlen(req->output);
		req->output = (char *) realloc(req->output, buflen+count+1);
		memcpy(req->output+buflen, ptr, count);
		*(req->output+buflen+count) = '\0';
	}

	return count;
}


#if (LIBCURL_VERSION_NUM >= 0x070a00)
static int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
	http_data_t *req = userp;
	char *p;

	if ((req->certexpires == 0) && (type == CURLINFO_TEXT)) {
		if (strncmp(data, "Server certificate:", 19) == 0) req->logcert = 1;
		else if (*data != '\t') req->logcert = 0;

		if (req->logcert) {
			if (req->certinfo == NULL) {
				req->certinfo = (char *) malloc(size+1);
				memcpy(req->certinfo, data, size);
				*(req->certinfo+size) = '\0';
			}
			else {
				size_t buflen = strlen(req->certinfo);
				req->certinfo = (char *) realloc(req->certinfo, buflen+size+1);
				memcpy(req->certinfo+buflen, data, size);
				*(req->certinfo+buflen+size) = '\0';
			}
		}

		p = strstr(data, "expire date:");
		if (p) req->certexpires = sslcert_expiretime(p + strlen("expire date:"));
	}

	return 0;
}
#endif

void run_http_tests(service_t *httptest, long followlocations, char *logfile, int sslcertcheck)
{
	http_data_t *req;
	testitem_t *t;
	char useragent[100];
	int maxtimeout = DEF_TIMEOUT;

#ifdef MULTICURL
	CURLM *multihandle;
	int multiactive;
#endif

	if (logfile) {
		logfd = fopen(logfile, "a");
		if (logfd) fprintf(logfd, "*** Starting web checks at %s ***\n", timestamp);
	}
	sprintf(useragent, "BigBrother bbtest-net/%s curl/%s-%s", VERSION, LIBCURL_VERSION, curl_version());

	for (t = httptest->items; (t); t = t->next) {
		req = (http_data_t *) t->privdata;
		
		req->curl = curl_easy_init();
		if (req->curl == NULL) {
			errprintf("ERROR: Cannot initialize curl session\n");
			return;
		}

		if (req->ip && req->hosthdr) {
			/*
			 * libcurl has no support for testing a specific IP-address.
			 * So we need to fake that: Substitute the hostname with the
			 * IP-address inside the URL, and set a "Host:" header
			 * so that virtual webhosts will work.
			 */
			curl_easy_setopt(req->curl, CURLOPT_URL, urlip(req->url, req->ip, NULL));
			req->slist = curl_slist_append(req->slist, req->hosthdr);
			curl_easy_setopt(req->curl, CURLOPT_HTTPHEADER, req->slist);
		}
		else {
			curl_easy_setopt(req->curl, CURLOPT_URL, req->url);
		}

		curl_easy_setopt(req->curl, CURLOPT_NOPROGRESS, 1);
		curl_easy_setopt(req->curl, CURLOPT_USERAGENT, useragent);

		/* Dont check if peer name in certificate is OK */
    		curl_easy_setopt(req->curl, CURLOPT_SSL_VERIFYPEER, 0);
    		curl_easy_setopt(req->curl, CURLOPT_SSL_VERIFYHOST, 0);

		curl_easy_setopt(req->curl, CURLOPT_TIMEOUT, (t->host->timeout ? t->host->timeout : DEF_TIMEOUT));
		curl_easy_setopt(req->curl, CURLOPT_CONNECTTIMEOUT, (t->host->conntimeout ? t->host->conntimeout : DEF_CONNECT_TIMEOUT));
		if (t->host->timeout > maxtimeout) maxtimeout = t->host->timeout;

		/* Activate our callbacks */
		curl_easy_setopt(req->curl, CURLOPT_WRITEHEADER, req);
		curl_easy_setopt(req->curl, CURLOPT_HEADERFUNCTION, hdr_callback);
		curl_easy_setopt(req->curl, CURLOPT_WRITEDATA, req);
		curl_easy_setopt(req->curl, CURLOPT_WRITEFUNCTION, data_callback);
		curl_easy_setopt(req->curl, CURLOPT_ERRORBUFFER, &req->errorbuffer);

#if (LIBCURL_VERSION_NUM >= 0x070a00)
		if (sslcertcheck && (!t->host->nosslcert) && (strncmp(req->url, "https:", 6) == 0)) {
			curl_easy_setopt(req->curl, CURLOPT_VERBOSE, 1);
			curl_easy_setopt(req->curl, CURLOPT_DEBUGDATA, req);
			curl_easy_setopt(req->curl, CURLOPT_DEBUGFUNCTION, debug_callback);
		}

		/* If needed, get username/password from $HOME/.netrc */
		curl_easy_setopt(req->curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
#endif

		/* Follow Location: headers for redirects? */
		if (followlocations) {
			curl_easy_setopt(req->curl, CURLOPT_FOLLOWLOCATION, 1);
			curl_easy_setopt(req->curl, CURLOPT_MAXREDIRS, followlocations);
		}

		/* Any post data ? */
		if (req->postdata) curl_easy_setopt(req->curl, CURLOPT_POSTFIELDS, req->postdata);

		/* Select SSL version, if requested */
		if (req->sslversion > 0) curl_easy_setopt(req->curl, CURLOPT_SSLVERSION, req->sslversion);

		/* Select SSL ciphers, if requested */
		if (req->ciphers) curl_easy_setopt(req->curl, CURLOPT_SSL_CIPHER_LIST, req->ciphers);

		/* Select proxy, if requested */
		if (req->proxy) {
			curl_easy_setopt(req->curl, CURLOPT_PROXY, req->proxy);

			/* Default only - may be overridden by specifying ":portnumber" in the proxy string. */
			curl_easy_setopt(req->curl, CURLOPT_PROXYPORT, 80);

			/* For proxies requiring authentication... */
			if (req->proxyuserpwd) {
				curl_easy_setopt(req->curl, CURLOPT_PROXYUSERPWD, req->proxyuserpwd);
			}
		}
	}

#ifndef MULTICURL
	for (t = httptest->items; (t); t = t->next) {

		/* Let's do it ... */

		req = (http_data_t *) t->privdata;

		if (logfd) fprintf(logfd, "\n*** Checking URL: %s ***\n", req->url);
		req->res = curl_easy_perform(req->curl);
	}
#else
	/* Setup the multi handle with all of the individual http transfers */
	multihandle = curl_multi_init();
	if (multihandle == NULL) {
		errprintf("Cannot initiate CURL multi handle - aborting HTTP tests\n");
		return;
	}
	for (t = httptest->items; (t); t = t->next) {
		req = (http_data_t *) t->privdata;

		curl_multi_add_handle(multihandle, req->curl);
	}

	/* Do the transfers */
	while (curl_multi_perform(multihandle, &multiactive) == CURLM_CALL_MULTI_PERFORM);
	while (multiactive) {
		struct timeval tmo;
		fd_set fdread;
		fd_set fdwrite;
		fd_set fdexcep;
		int maxfd, selres, msgsleft;
		CURLMsg *msg;

		/* Setup the file descriptors */
		FD_ZERO(&fdread); FD_ZERO(&fdwrite); FD_ZERO(&fdexcep);
		curl_multi_fdset(multihandle, &fdread, &fdwrite, &fdexcep, &maxfd);

		/* 
		 * Setup the timeout.
		 * It seems this should be slightly more than the longest timeout
		 * for any of the individual transfers. A smaller value causes
		 * us to get "timeout" responses for lots of transfers from libcurl.
		 */
		tmo.tv_sec = maxtimeout+5;
		tmo.tv_usec = 0;

		dprintf("curl_multi select with %d handles active\n", multiactive);
		selres = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &tmo);

		switch (selres) {
			case -1:
				errprintf("select() returned an error - aborting http tests\n");
				multiactive = 0;
				break;

			case 0:
				/*
				 * Timeout: We must still call curl_multi_perform()
				 * for the library to fill in information about how the
				 * timeout occurred (dns tmo, connect tmo, tmo waiting for data ...)
				 * So fall through to the default handler!
				 */
				dprintf("timeout waiting for http requests\n");
			default:
				/* Do some data processing */
				while (curl_multi_perform(multihandle, &multiactive) == CURLM_CALL_MULTI_PERFORM);
				break;
		}

		/* Pick up the result codes. Odd way of doing it, but so says libcurl */
		while ((msg = curl_multi_info_read(multihandle, &msgsleft))) {
			if (msg->msg == CURLMSG_DONE) {
				int found = 0;

				for (t = httptest->items; (t && !found); t = t->next) {
					req = (http_data_t *) t->privdata;

					found = (req->curl == msg->easy_handle);
					if (found) req->res = msg->data.result;
				}
			}
		}
	}

	curl_multi_cleanup(multihandle);
#endif

	for (t = httptest->items; (t); t = t->next) {
		req = (http_data_t *) t->privdata;

		if (req->res == CURL_LAST) {
			errprintf("libcurl never provided a result for %s - assuming timeout\n", req->url);
			req->res = CURLE_OPERATION_TIMEOUTED;
			strcpy(req->errorbuffer, "libcurl weirdness - assuming timeout");
		}

		if (req->res != CURLE_OK) {
			/* Some error occurred */
			req->headers = (char *) malloc(strlen(req->errorbuffer) + 20);
			sprintf(req->headers, "Error %3d: %s\n\n", req->res, req->errorbuffer);
			if (logfd) fprintf(logfd, "Error %d: %s\n", req->res, req->errorbuffer);
			t->open = 0;
		}
		else {
			double t1, t2;
			char *contenttype;

			curl_easy_getinfo(req->curl, CURLINFO_HTTP_CODE, &req->httpstatus);
			curl_easy_getinfo(req->curl, CURLINFO_CONNECT_TIME, &t1);
			curl_easy_getinfo(req->curl, CURLINFO_TOTAL_TIME, &t2);
			curl_easy_getinfo(req->curl, CURLINFO_CONTENT_TYPE, &contenttype);
			req->totaltime = t1+t2;
			req->errorbuffer[0] = '\0';
			req->contenttype = (contenttype ? malcop(contenttype) : "");
			t->open = 1;
		}

		if (req->slist) curl_slist_free_all(req->slist);
		curl_easy_cleanup(req->curl);
	}

	if (logfd) fclose(logfd);
}


void send_http_results(service_t *httptest, testedhost_t *host, char *nonetpage, 
		       char *contenttestname, int failgoesclear)
{
	testitem_t *t;
	int	color = -1;
	char	msgline[MAXMSG];
	char	msgtext[MAXMSG];
	char    *nopagename;
	int     nopage = 0;
	int 	contentnum = 0;
	char 	*conttest = (char *) malloc(strlen(contenttestname)+5);
	testitem_t *http1 = host->firsthttp;
	int	anydown = 0;

	if (http1 == NULL) return;

	/* Check if this service is a NOPAGENET service. */
	nopagename = (char *) malloc(strlen(httptest->testname)+3);
	sprintf(nopagename, ",%s,", httptest->testname);
	nopage = (strstr(nonetpage, httptest->testname) != NULL);
	free(nopagename);

	dprintf("Calc http color host %s : ", host->hostname);
	msgtext[0] = '\0';
	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = (http_data_t *) t->privdata;

		req->httpcolor = statuscolor(host, req->httpstatus);
		if (req->httpcolor == COL_RED) anydown++;

		/* Dialup hosts and dialup tests report red as clear */
		if ((req->httpcolor != COL_GREEN) && (host->dialup || t->dialup)) req->httpcolor = COL_CLEAR;

		/* If ping failed, report CLEAR unless alwaystrue */
		if ( ((req->httpcolor == COL_RED) || (req->httpcolor == COL_YELLOW)) && /* Test failed */
		     (host->downcount > 0)                   && /* The ping check did fail */
		     (!host->noping && !host->noconn)        && /* We are doing a ping test */
		     (failgoesclear)                         &&
		     (!t->alwaystrue)                           )  /* No "~testname" flag */ {
			req->httpcolor = COL_CLEAR;
		}

		/* If test we depend on has failed, report CLEAR unless alwaystrue */
		if ( ((req->httpcolor == COL_RED) || (req->httpcolor == COL_YELLOW)) && /* Test failed */
		      failgoesclear && !t->alwaystrue )  /* No "~testname" flag */ {
			char *faileddeps = deptest_failed(host, t->service->testname);

			if (faileddeps) {
				req->httpcolor = COL_CLEAR;
				req->faileddeps = malcop(faileddeps);
			}
		}

		dprintf("%s(%s) ", t->testspec, colorname(req->httpcolor));
		if (req->httpcolor > color) color = req->httpcolor;

		if (req->headers) {
			char    *firstline;
			int	len;
			char	savechar;

			strcat(msgtext, (strlen(msgtext) ? " ; " : ": ") );
			for (firstline = req->headers; (*firstline && isspace((int) *firstline)); firstline++);
			len = strcspn(firstline, "\r\n");
			if (len  > (sizeof(msgtext)-strlen(msgtext)-2)) len = sizeof(msgtext) - strlen(msgtext) - 2;
			savechar = *(firstline+len);
			*(firstline+len) = '\0';
			strcat(msgtext, firstline);
			*(firstline+len) = savechar;
		}
	}

	if (anydown) http1->downcount++; else http1->downcount = 0;

	/* Handle the "badtest" stuff for http tests */
	if ((color == COL_RED) && (http1->downcount < http1->badtest[2])) {
		if      (http1->downcount >= http1->badtest[1]) color = COL_YELLOW;
		else if (http1->downcount >= http1->badtest[0]) color = COL_CLEAR;
		else                                            color = COL_GREEN;
	}

	if (nopage && (color == COL_RED)) color = COL_YELLOW;
	dprintf(" --> %s\n", colorname(color));

	/* Send off the http status report */
	init_status(color);
	sprintf(msgline, "status %s.%s %s %s", 
		commafy(host->hostname), httptest->testname, colorname(color), timestamp);
	addtostatus(msgline);
	addtostatus(msgtext);
	addtostatus("\n");

	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = (http_data_t *) t->privdata;

		if (req->ip == NULL) {
			sprintf(msgline, "\n&%s %s - %s\n", colorname(req->httpcolor), req->url,
				((req->httpcolor != COL_GREEN) ? "failed" : "OK"));
		}
		else {
			sprintf(msgline, "\n&%s (IP: %s) %s - %s\n", colorname(req->httpcolor), 
				req->url, req->ip,
				((req->httpcolor != COL_GREEN) ? "failed" : "OK"));
		}
		addtostatus(msgline);
		sprintf(msgline, "\n%s", req->headers);
		addtostatus(msgline);
		if (req->faileddeps) addtostatus(req->faileddeps);

		sprintf(msgline, "Seconds: %5.2f\n", req->totaltime);
		addtostatus(msgline);
	}
	addtostatus("\n\n");
	finish_status();
	
	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = (http_data_t *) t->privdata;
		char cause[100];
		int got_data = 1;

		strcpy(cause, "Content OK");
		if (req->exp) {
			/* We have a content check */
			if (req->contstatus == 0) {
				/* The content check passed initial checks of regexp etc. */
				color = statuscolor(t->host, req->httpstatus);
				if (color == COL_GREEN) {
					/* We got the data from the server */
					regmatch_t foo[1];
					int status;

					if (req->output) {
						status = regexec(req->exp, req->output, 0, foo, 0);
						regfree(req->exp);
					}
					else {
						/* output may be null if we only got a redirect */
						status = STATUS_CONTENTMATCH_FAILED;
					}
					req->contstatus = ((status == 0)  ? 200 : STATUS_CONTENTMATCH_FAILED);
					color = statuscolor(t->host, req->contstatus);
					if (color != COL_GREEN) strcpy(cause, "Content match failed");
				}
				else {
					/*
					 * Failed to retrieve the webpage.
					 * Report CLEAR, unless "alwaystrue" is set.
					 */
					if (failgoesclear && !t->alwaystrue) color = COL_CLEAR;
					got_data = 0;
					strcpy(cause, "Failed to get webpage");
				}

				/* If not inside SLA and non-green, report as BLUE */
				if (!t->host->okexpected && (color != COL_GREEN)) color = COL_BLUE;

				if (nopage && (color == COL_RED)) color = COL_YELLOW;
			}
			else {
				/* This only happens upon internal errors in BB test system */
				color = statuscolor(t->host, req->contstatus);
				strcpy(cause, "Internal BB error");
			}

			/* Send of the status */
			dprintf("Content check on %s is %s\n", req->url, colorname(color));

			if (contentnum > 0) sprintf(conttest, "%s%d", contenttestname, contentnum);
			else strcpy(conttest, contenttestname);

			init_status(color);
			sprintf(msgline, "status %s.%s %s %s: %s\n", 
				commafy(host->hostname), conttest, colorname(color), timestamp, cause);
			addtostatus(msgline);

			if (!got_data) {
				sprintf(msgline, "\nAn HTTP error occurred while testing <a href=\"%s\">URL %s</a>\n", 
					req->url, req->url);
			}
			else {
				sprintf(msgline, "\n&%s %s - Testing <a href=\"%s\">URL</a> yields:\n",
					colorname(color), req->url, req->url);
			}
			addtostatus(msgline);

			if (req->output) {
				if ( (strcasecmp(req->contenttype, "text/html") == 0) ||
				     (strncasecmp(req->output, "<html", 5) == 0) ) {
					char *bodystart = NULL;
					char *bodyend = NULL;

					bodystart = strstr(req->output, "<body");
					if (bodystart == NULL) bodystart = strstr(req->output, "<BODY");
					if (bodystart) {
						char *p;

						p = strchr(bodystart, '>');
						if (p) bodystart = (p+1);
					}
					else bodystart = req->output;

					bodyend = strstr(bodystart, "</body");
					if (bodyend == NULL) bodyend = strstr(bodystart, "</BODY");
					if (bodyend) {
						*bodyend = '\0';
					}

					addtostatus("<div>\n");
					addtostatus(bodystart);
					addtostatus("\n</div>\n");
				}
				else {
					addtostatus(req->output);
				}
			}
			else {
				addtostatus("\nNo output received from server\n\n");
			}

			addtostatus("\n\n");
			finish_status();

			contentnum++;
		}
	}

	free(conttest);

}


void show_http_test_results(service_t *httptest)
{
	http_data_t *req;
	testitem_t *t;

	for (t = httptest->items; (t); t = t->next) {
		req = (http_data_t *) t->privdata;

		printf("URL                      : %s\n", req->url);
		printf("Req. SSL version/ciphers : %d/%s\n", req->sslversion, textornull(req->ciphers));
		printf("HTTP status              : %lu\n", req->httpstatus);
		printf("Time spent               : %f\n", req->totaltime);
		printf("HTTP headers\n%s\n", textornull(req->headers));
		printf("HTTP output\n%s\n", textornull(req->output));
		printf("curl error data:\n%s\n", req->errorbuffer);
		printf("------------------------------------------------------\n");
	}
}

