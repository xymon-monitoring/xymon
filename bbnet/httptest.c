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

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbtest-net.h"


typedef struct {
	char   *url;
	int    sslversion;		/* SSL version CURLOPT_SSLVERSION */
	char   *ciphers; 	   	/* SSL ciphers CURLOPT_SSL_CIPHER_LIST */
	char   *proxy;                  /* Proxy host CURLOPT_PROXY */
	CURL   *curl;			/* Handle for libcurl */
	char   errorbuffer[CURL_ERROR_SIZE];	/* Error buffer for libcurl */
	long   httpstatus;		/* HTTP status from server */
	long   contstatus;		/* Status of content check */
	char   *headers;                /* HTTP headers from server */
	char   *output;                 /* Data from server */
	double totaltime;		/* Time spent doing request */
	char   *expoutput;              /* Expected output, if content check. */
	regex_t *exp;			/* regexp data for content match */
} http_data_t;


void add_http_test(testitem_t *t)
{
	/* See http://www.openssl.org/docs/apps/ciphers.html for cipher strings */
	static char *ciphersmedium = "MEDIUM";	/* Must be formatted for openssl library */
	static char *ciphershigh = "HIGH";	/* Must be formatted for openssl library */

	http_data_t *req;
	char *proto = NULL;
	char *proxy = NULL;
	int status;

	/* Allocate the private data and initialize it */
	t->private = req = malloc(sizeof(http_data_t));
	req->url = malcop(realurl(t->testspec, &proxy));
	if (proxy) req->proxy = malcop(proxy); else req->proxy = NULL;
	req->sslversion = 0;
	req->ciphers = NULL;
	req->proxy = NULL;
	req->curl = NULL;
	req->errorbuffer[0] = '\0';
	req->httpstatus = 0;
	req->contstatus = 0;
	req->headers = NULL;
	req->output = NULL;
	req->totaltime = 0.0;
	req->expoutput = NULL;
	req->exp = NULL;

	/* 
	 * t->testspec containts the full testspec
	 * It can be either a URL, a "content=URL", or "cont;URL;expected_data"
	 */

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
				req->expoutput = malcop(p);
				req->exp = malloc(sizeof(regex_t));
				status = regcomp(req->exp, p, REG_EXTENDED|REG_NOSUB);
				if (status) {
					printf("Failed to compile regexp '%s' for URL %s\n", p, req->url);
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
			req->expoutput = malcop(p+1);
			req->exp = malloc(sizeof(regex_t));
			status = regcomp(req->exp, p+1, REG_EXTENDED|REG_NOSUB);
			if (status) {
				printf("Failed to compile regexp '%s' for URL %s\n", p+1, req->url);
				req->contstatus = STATUS_CONTENTMATCH_BADREGEX;
			}
		}
		else req->contstatus = STATUS_CONTENTMATCH_NOFILE;
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



int statuscolor(testedhost_t *h, long status)
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
	if ((result >= COL_YELLOW) && (!h->in_sla)) {
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

	if (req->headers == NULL) {
		req->headers = malloc(count+1);
		memcpy(req->headers, ptr, count);
		*(req->headers+count) = '\0';
	}
	else {
		size_t buflen = strlen(req->headers);
		req->headers = realloc(req->headers, buflen+count+1);
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

	if (req->expoutput == NULL) {
		/* No need to save output - just drop it */
		return count;
	}

	if (req->output == NULL) {
		req->output = malloc(count+1);
		memcpy(req->output, ptr, count);
		*(req->output+count) = '\0';
	}
	else {
		size_t buflen = strlen(req->output);
		req->output = realloc(req->output, buflen+count+1);
		memcpy(req->output+buflen, ptr, count);
		*(req->output+buflen+count) = '\0';
	}

	return count;
}


void run_http_tests(service_t *httptest)
{
	http_data_t *req;
	testitem_t *t;
	CURLcode res;

	if (curl_global_init(CURL_GLOBAL_DEFAULT)) {
		printf("FATAL: Cannot initialize libcurl!\n");
		return;
	}

	for (t = httptest->items; (t); t = t->next) {
		req = t->private;
		
		req->curl = curl_easy_init();
		if (req->curl == NULL) {
			printf("ERROR: Cannot initialize curl session\n");
			return;
		}

		curl_easy_setopt(req->curl, CURLOPT_URL, req->url);
		curl_easy_setopt(req->curl, CURLOPT_NOPROGRESS, 1);
		curl_easy_setopt(req->curl, CURLOPT_WRITEHEADER, req);
		curl_easy_setopt(req->curl, CURLOPT_HEADERFUNCTION, hdr_callback);
    		curl_easy_setopt(req->curl, CURLOPT_SSL_VERIFYPEER, 0);
		curl_easy_setopt(req->curl, CURLOPT_TIMEOUT, (t->host->timeout ? t->host->timeout : DEF_TIMEOUT));
		curl_easy_setopt(req->curl, CURLOPT_CONNECTTIMEOUT, (t->host->conntimeout ? t->host->conntimeout : DEF_CONNECT_TIMEOUT));
		curl_easy_setopt(req->curl, CURLOPT_WRITEDATA, req);
		curl_easy_setopt(req->curl, CURLOPT_WRITEFUNCTION, data_callback);
		curl_easy_setopt(req->curl, CURLOPT_ERRORBUFFER, &req->errorbuffer);

		/* If needed, get username/password from $HOME/.netrc */
		curl_easy_setopt(req->curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

		/* Select SSL version, if requested */
		if (req->sslversion > 0) curl_easy_setopt(req->curl, CURLOPT_SSLVERSION, req->sslversion);

		/* Select SSL ciphers, if requested */
		if (req->ciphers) curl_easy_setopt(req->curl, CURLOPT_SSL_CIPHER_LIST, req->ciphers);

		/* Select proxy, if requested */
		if (req->proxy) curl_easy_setopt(req->curl, CURLOPT_PROXY, req->proxy);

		/* Let's do it ... */
		res = curl_easy_perform(req->curl);
		if (res != 0) {
			/* Some error occurred */
			strcat(req->errorbuffer, "\n\n");
			req->headers = malcop(req->errorbuffer);
		}
		else {
			double t1, t2;

			curl_easy_getinfo(req->curl, CURLINFO_HTTP_CODE, &req->httpstatus);
			curl_easy_getinfo(req->curl, CURLINFO_CONNECT_TIME, &t1);
			curl_easy_getinfo(req->curl, CURLINFO_TOTAL_TIME, &t2);
			req->totaltime = t1+t2;
			req->errorbuffer[0] = '\0';
		}

		curl_easy_cleanup(req->curl);
	}

	curl_global_cleanup();
}


void send_http_results(service_t *httptest, testedhost_t *host, char *nonetpage, char *contenttestname)
{
	testitem_t *t;
	int	color = -1;
	char	msgline[MAXMSG];
	char    *nopagename;
	int     nopage = 0;
	int 	contentnum = 0;
	char 	*conttest = malloc(strlen(contenttestname)+5);

	if (host->firsthttp == NULL) return;

	/* Check if this service is a NOPAGENET service. */
	nopagename = malloc(strlen(httptest->testname)+3);
	sprintf(nopagename, ",%s,", httptest->testname);
	nopage = (strstr(nonetpage, httptest->testname) != NULL);
	free(nopagename);

	dprintf("Calc http color host %s : ", host->hostname);
	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = t->private;

		int httpcolor = statuscolor(host, req->httpstatus);
		dprintf("%s(%s) ", t->testspec, colorname(httpcolor));
		if (httpcolor > color) color = httpcolor;
	}
	if (nopage && (color == COL_RED)) color = COL_YELLOW;
	dprintf(" --> %s\n", colorname(color));

	/* Send off the http status report */
	init_status(color);
	sprintf(msgline, "status %s.%s %s %s\n", 
		commafy(host->hostname), httptest->testname, colorname(color), timestamp);
	addtostatus(msgline);

	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = t->private;

		int httpcolor = statuscolor(host, req->httpstatus);

		sprintf(msgline, "\n&%s %s - %s\n", colorname(httpcolor), req->url,
			((httpcolor != COL_GREEN) ? "failed" : "OK"));
		addtostatus(msgline);
		sprintf(msgline, "\n%s", req->headers);
		addtostatus(msgline);

		sprintf(msgline, "Seconds: %5.2f\n", req->totaltime);
		addtostatus(msgline);
	}
	addtostatus("\n\n");
	finish_status();
	
	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = t->private;

		if (req->expoutput) {
			/* We have a content check */
			if (req->contstatus == 0) {
				/* The content check passed initial checks of regexp etc. */
				if (statuscolor(t->host, req->httpstatus) == COL_GREEN) {
					/* We got the data from the server */
					regmatch_t foo[1];
					int status;

					status = regexec(req->exp, req->output, 0, foo, 0);
					regfree(req->exp);
					req->contstatus = ((status == 0)  ? 200 : STATUS_CONTENTMATCH_FAILED);
					color = statuscolor(t->host, req->contstatus);
				}
				else {
					/* Failed to retrieve the webpage */
					color = COL_CLEAR;
				}
			}
			else color = statuscolor(t->host, req->contstatus);

			/* Send of the status */
			dprintf("Content check on %s is %s\n", req->url, colorname(color));

			if (contentnum > 0) sprintf(conttest, "%s%d", contenttestname, contentnum);
			else strcpy(conttest, contenttestname);

			init_status(color);
			sprintf(msgline, "status %s.%s %s %s\n", 
				commafy(host->hostname), conttest, colorname(color), timestamp);
			addtostatus(msgline);

			if (color == COL_CLEAR) {
				sprintf(msgline, "\nAn HTTP error occurred while testing <a href=\"%s\">URL %s</a>\n", 
					realurl(req->url, NULL), realurl(req->url, NULL));
			}
			else {
				sprintf(msgline, "\n&%s %s - Testing <a href=\"%s\">URL</a> yields:\n",
					colorname(color), realurl(req->url, NULL), realurl(req->url, NULL));
			}
			addtostatus(msgline);

			if (req->output) {
				addtostatus("<pre>\n");
				addtostatus(req->output);
				addtostatus("\n</pre><br>\n");
			}
			else {
				addtostatus("\n<p>No output received from server</p><br>\n");
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
		req = t->private;

		printf("URL                      : %s\n", req->url);
		printf("Req. SSL version/ciphers : %d/%s\n", req->sslversion, req->ciphers);
		printf("Expected output          : %s\n", textornull(req->expoutput));
		printf("HTTP status              : %lu\n", req->httpstatus);
		printf("Time spent               : %f\n", req->totaltime);
		printf("HTTP headers\n%s\n", req->headers);
		printf("HTTP output\n%s\n", textornull(req->output));
		printf("curl error data:\n%s\n", req->errorbuffer);
		printf("------------------------------------------------------\n");
	}
}

