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

static char rcsid[] = "$Id: httptest.c,v 1.21 2003-06-23 08:54:30 henrik Exp $";

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
	char   *url;                    /* URL to request, stripped of BB'isms */
	char   *proxy;                  /* Proxy host CURLOPT_PROXY */
	char   *postdata;               /* Form POST data CURLOPT_POSTFIELDS */
	int    sslversion;		/* SSL version CURLOPT_SSLVERSION */
	char   *ciphers; 	   	/* SSL ciphers CURLOPT_SSL_CIPHER_LIST */
	CURL   *curl;			/* Handle for libcurl */
	char   errorbuffer[CURL_ERROR_SIZE];	/* Error buffer for libcurl */
	long   httpstatus;		/* HTTP status from server */
	long   contstatus;		/* Status of content check */
	char   *headers;                /* HTTP headers from server */
	char   *output;                 /* Data from server */
	int    logcert;
	char   *sslinfo;                /* Data about SSL certificate */
	time_t sslexpire;               /* Expiry time for SSL cert */
	int    httpcolor;
	char   *faileddeps;
	int    sslcolor;
	double totaltime;		/* Time spent doing request */
	regex_t *exp;			/* regexp data for content match */
} http_data_t;

static FILE *logfd = NULL;


char *http_library_version = NULL;
static int can_ssl = 1;
static int can_ldap = 0;

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
	int status;

	/* 
	 * t->testspec containts the full testspec
	 * It can be either "URL", "content=URL", 
	 * "cont;URL;expected_data", "post;URL;postdata;expected_data"
	 */

	/* Allocate the private data and initialize it */
	t->private = req = malloc(sizeof(http_data_t));
	req->url = malcop(realurl(t->testspec, &proxy));
	if (proxy) req->proxy = malcop(proxy); else req->proxy = NULL;
	req->postdata = NULL;
	req->sslversion = 0;
	req->ciphers = NULL;
	req->curl = NULL;
	req->errorbuffer[0] = '\0';
	req->httpstatus = 0;
	req->contstatus = 0;
	req->headers = NULL;
	req->output = NULL;
	req->httpcolor = -1;
	req->faileddeps = NULL;
	req->sslcolor = -1;
	req->logcert = 0;
	req->sslinfo = NULL;
	req->sslexpire = 0;
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
				req->exp = malloc(sizeof(regex_t));
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
			req->exp = malloc(sizeof(regex_t));
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
				req->exp = malloc(sizeof(regex_t));
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

	if (logfd) fprintf(logfd, "%s", (char *)ptr);

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

	if (logfd) fprintf(logfd, "%s", (char *)ptr);

	if (req->exp == NULL) {
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


#if (LIBCURL_VERSION_NUM >= 0x070a00)
static int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
	http_data_t *req = userp;
	char *p;
	struct tm exptime;

	if ((req->sslexpire == 0) && (type == CURLINFO_TEXT)) {
		if (strncmp(data, "Server certificate:", 19) == 0) req->logcert = 1;
		else if (*data != '\t') req->logcert = 0;

		if (req->logcert) {
			if (req->sslinfo == NULL) {
				req->sslinfo = malloc(size+1);
				memcpy(req->sslinfo, data, size);
				*(req->sslinfo+size) = '\0';
			}
			else {
				size_t buflen = strlen(req->sslinfo);
				req->sslinfo = realloc(req->sslinfo, buflen+size+1);
				memcpy(req->sslinfo+buflen, data, size);
				*(req->sslinfo+buflen+size) = '\0';
			}
		}

		p = strstr(data, "expire date:");
		if (p) {
			int res;
			time_t t1, t2;
			struct tm *t;
			time_t gmtofs;

			/* expire date: 2004-01-02 08:04:15 GMT */
			res = sscanf(p, "expire date: %4d-%2d-%2d %2d:%2d:%2d", 
				     &exptime.tm_year, &exptime.tm_mon, &exptime.tm_mday,
				     &exptime.tm_hour, &exptime.tm_min, &exptime.tm_sec);
			/* tm_year is 1900 based; tm_mon is 0 based */
			exptime.tm_year -= 1900; exptime.tm_mon -= 1;
			req->sslexpire = mktime(&exptime);

			/* 
			 * Calculate the difference between localtime and GMT 
			 */
			t = gmtime(&req->sslexpire); t->tm_isdst = 0; t1 = mktime(t);
			t = localtime(&req->sslexpire); t->tm_isdst = 0; t2 = mktime(t);
			gmtofs = (t2-t1);

			req->sslexpire += gmtofs;
			// printf("Output says it expires: %s", p);
			// printf("I think it expires at (localtime) %s\n", asctime(localtime(&req->sslexpire)));
		}

	}

	return 0;
}
#endif

void run_http_tests(service_t *httptest, long followlocations, char *logfile, int sslcertcheck)
{
	http_data_t *req;
	testitem_t *t;
	CURLcode res;
	char useragent[100];

	if (logfile) {
		logfd = fopen(logfile, "a");
		if (logfd) fprintf(logfd, "*** Starting web checks at %s ***\n", timestamp);
	}
	sprintf(useragent, "BigBrother bbtest-net/%s curl/%s", VERSION, LIBCURL_VERSION);

	for (t = httptest->items; (t); t = t->next) {
		req = t->private;
		
		req->curl = curl_easy_init();
		if (req->curl == NULL) {
			errprintf("ERROR: Cannot initialize curl session\n");
			return;
		}

		curl_easy_setopt(req->curl, CURLOPT_URL, req->url);
		curl_easy_setopt(req->curl, CURLOPT_NOPROGRESS, 1);
		curl_easy_setopt(req->curl, CURLOPT_USERAGENT, useragent);

		/* Dont check if peer name in certificate is OK */
    		curl_easy_setopt(req->curl, CURLOPT_SSL_VERIFYPEER, 0);
    		curl_easy_setopt(req->curl, CURLOPT_SSL_VERIFYHOST, 0);

		curl_easy_setopt(req->curl, CURLOPT_TIMEOUT, (t->host->timeout ? t->host->timeout : DEF_TIMEOUT));
		curl_easy_setopt(req->curl, CURLOPT_CONNECTTIMEOUT, (t->host->conntimeout ? t->host->conntimeout : DEF_CONNECT_TIMEOUT));

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
		}


		/* Let's do it ... */
		if (logfd) fprintf(logfd, "\n*** Checking URL: %s ***\n", req->url);
		res = curl_easy_perform(req->curl);
		if (res != 0) {
			/* Some error occurred */
			strcat(req->errorbuffer, "\n\n");
			req->headers = malcop(req->errorbuffer);
			if (logfd) fprintf(logfd, "Error: %s\n", req->errorbuffer);
			t->open = 0;
		}
		else {
			double t1, t2;

			curl_easy_getinfo(req->curl, CURLINFO_HTTP_CODE, &req->httpstatus);
			curl_easy_getinfo(req->curl, CURLINFO_CONNECT_TIME, &t1);
			curl_easy_getinfo(req->curl, CURLINFO_TOTAL_TIME, &t2);
			req->totaltime = t1+t2;
			req->errorbuffer[0] = '\0';
			t->open = 1;
		}

		curl_easy_cleanup(req->curl);
	}

	if (logfd) fclose(logfd);
}


void send_http_results(service_t *httptest, testedhost_t *host, char *nonetpage, 
		       char *contenttestname, char *ssltestname,
		       int sslwarndays, int sslalarmdays)
{
	testitem_t *t;
	int	color = -1;
	char	msgline[MAXMSG];
	char    *nopagename;
	int     nopage = 0;
	int 	contentnum = 0;
	char 	*conttest = malloc(strlen(contenttestname)+5);
	time_t  now = time(NULL);

	if (host->firsthttp == NULL) return;

	/* Check if this service is a NOPAGENET service. */
	nopagename = malloc(strlen(httptest->testname)+3);
	sprintf(nopagename, ",%s,", httptest->testname);
	nopage = (strstr(nonetpage, httptest->testname) != NULL);
	free(nopagename);

	dprintf("Calc http color host %s : ", host->hostname);
	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = t->private;

		req->httpcolor = statuscolor(host, req->httpstatus);

		/* Dialup hosts and dialup tests report red as clear */
		if ((req->httpcolor != COL_GREEN) && (host->dialup || t->dialup)) req->httpcolor = COL_CLEAR;

		/* If ping failed, report CLEAR unless alwaystrue */
		if ( ((req->httpcolor == COL_RED) || (req->httpcolor == COL_YELLOW)) && /* Test failed */
		     (host->downcount > 0)                   && /* The ping check did fail */
		     (!host->noping && !host->noconn)        && /* We are doing a ping test */
		     (!t->alwaystrue)                           )  /* No "~testname" flag */ {
			req->httpcolor = COL_CLEAR;
		}

		/* If test we depend on has failed, report CLEAR unless alwaystrue */
		if ( ((req->httpcolor == COL_RED) || (req->httpcolor == COL_YELLOW)) && /* Test failed */
		     (!t->alwaystrue)                              )  /* No "~testname" flag */ {
			char *faileddeps = deptest_failed(host, t->service->testname);

			if (faileddeps) {
				req->httpcolor = COL_CLEAR;
				req->faileddeps = malcop(faileddeps);
			}
		}

		dprintf("%s(%s) ", t->testspec, colorname(req->httpcolor));
		if (req->httpcolor > color) color = req->httpcolor;
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

		sprintf(msgline, "\n&%s %s - %s\n", colorname(req->httpcolor), req->url,
			((req->httpcolor != COL_GREEN) ? "failed" : "OK"));
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
		http_data_t *req = t->private;

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
				}
				else {
					/*
					 * Failed to retrieve the webpage.
					 * Report CLEAR, unless "alwaystrue" is set.
					 */
					if (!t->alwaystrue) color = COL_CLEAR;
				}

				/* If not inside SLA and non-green, report as BLUE */
				if (!t->host->in_sla && (color != COL_GREEN)) color = COL_BLUE;

				if (nopage && (color == COL_RED)) color = COL_YELLOW;
			}
			else {
				/* This only happens upon internal errors in BB test system */
				color = statuscolor(t->host, req->contstatus);
			}

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

	color = -1;
	for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
		http_data_t *req = t->private;

		if (req->sslinfo && (req->sslexpire > 0)) {
			req->sslcolor = COL_GREEN;

			if (req->sslexpire < (now+sslwarndays*86400)) req->sslcolor = COL_YELLOW;
			if (req->sslexpire < (now+sslalarmdays*86400)) req->sslcolor = COL_RED;
			if (req->sslcolor > color) color = req->sslcolor;
		}
	}

	if (color != -1) {
		/* Send off the sslcert status report */
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n", 
			commafy(host->hostname), ssltestname, colorname(color), timestamp);
		addtostatus(msgline);

		for (t=host->firsthttp; (t && (t->host == host)); t = t->next) {
			http_data_t *req = t->private;

			if (req->sslinfo && (req->sslexpire > 0)) {
				sprintf(msgline, "\n&%s SSL certificate for %s expires in %ld days\n\n", 
					colorname(req->sslcolor), realurl(req->url, NULL), (req->sslexpire-now) / 86400);
				addtostatus(msgline);
				addtostatus(req->sslinfo);
			}
		}
		addtostatus("\n\n");
		finish_status();
	}
}


void show_http_test_results(service_t *httptest)
{
	http_data_t *req;
	testitem_t *t;

	for (t = httptest->items; (t); t = t->next) {
		req = t->private;

		printf("URL                      : %s\n", req->url);
		printf("Req. SSL version/ciphers : %d/%s\n", req->sslversion, req->ciphers);
		printf("HTTP status              : %lu\n", req->httpstatus);
		printf("Time spent               : %f\n", req->totaltime);
		printf("HTTP headers\n%s\n", req->headers);
		printf("HTTP output\n%s\n", textornull(req->output));
		printf("curl error data:\n%s\n", req->errorbuffer);
		printf("------------------------------------------------------\n");
	}
}

