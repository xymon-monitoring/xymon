/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the testing of a HTTP service.                   */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HTTPTEST_H_
#define __HTTPTEST_H_

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#include <sys/types.h>
#include <time.h>
#include <regex.h>

#define CONTENTCHECK_NONE   0
#define CONTENTCHECK_REGEX  1
#define CONTENTCHECK_DIGEST 2
#define CONTENTCHECK_NOREGEX 3
#define CONTENTCHECK_CONTENTTYPE 4

#define HTTPVER_ANY 0
#define HTTPVER_10  1
#define HTTPVER_11  2

typedef struct {
	char   *url;                    /* URL to request, stripped of BB'isms */
	char   *proxy;                  /* Proxy host CURLOPT_PROXY */
	char   *proxyuserpwd;           /* Proxy username/passwd CURLOPT_PROXYUSERPWD */
	char   *ip;                     /* IP to test against */
	int    is_ftp;                  /* Set if URL is an FTP request */
	int    httpver;                 /* If we must do only HTTP 1.0 */
	char   *hosthdr;                /* Host: header for ip-based test */
	char   *postdata;               /* Form POST data CURLOPT_POSTFIELDS */
	int    sslversion;		/* SSL version CURLOPT_SSLVERSION */
	char   *ciphers; 	   	/* SSL ciphers CURLOPT_SSL_CIPHER_LIST */
	CURL   *curl;			/* Handle for libcurl */
	struct curl_slist *slist;	/* libcurl replacement header list */
	CURLcode res;			/* libcurl result code */
	char   errorbuffer[CURL_ERROR_SIZE];	/* Error buffer for libcurl */
	long   httpstatus;		/* HTTP status from server */
	long   contstatus;		/* Status of content check */
	char   *headers;                /* HTTP headers from server */
	char   *contenttype;		/* Content-type: header */
	char   *output;                 /* Data from server */
	char   *digest;                 /* Digest of server data */
	int    logcert;
	char   *certinfo;               /* Data about SSL certificate */
	time_t certexpires;		/* Expiry time for SSL cert */
	int    httpcolor;
	char   *faileddeps;
	double totaltime;		/* Time spent doing request */
	int    contentcheck;            /* 0=no content check, 1=regex check, 2=digest check */
	void   *exp;			/* data for content match (digest, or regexp data) */
} http_data_t;

extern char *http_library_version;
extern int  init_http_library(void);
extern void shutdown_http_library(void);

extern void add_http_test(testitem_t *t);
extern void run_http_tests(service_t *httptest, long followlocations, char *logfile, int sslcertcheck);
extern void show_http_test_results(service_t *httptest);
extern void send_http_results(service_t *httptest, testedhost_t *host, testitem_t *firsttest,
			      char *nonetpage, int failgoesclear);
extern void send_content_results(service_t *httptest, service_t *ftptest, testedhost_t *host,
				 char *nonetpage, char *contenttestname, int failgoesclear);

#endif

