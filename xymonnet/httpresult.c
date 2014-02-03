/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* This is used to implement the testing of HTTP service.                     */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <sys/stat.h>

#include "libxymon.h"

#include "xymonnet.h"
#include "contest.h"
#include "httpcookies.h"
#include "httpresult.h"

static int statuscolor(testedhost_t *h, long status)
{
	int result;

	switch(status) {
	  case 000:			/* transportlayer reports error */
		result = (h->dialup ? COL_CLEAR : COL_RED);
		break;
	  case 100: /* Continue - should be ok */
	  case 200: case 201: case 202: case 203: case 204: case 205: case 206:
	  case 301: case 302: case 303: case 307:
	  case 401: case 403: 		/* Is "Forbidden" an OK status ? */
		result = COL_GREEN;
		break;
	  case 400: case 404: case 405: case 406:
		result = COL_RED;	/* Trouble getting page */
		break;
	  case 500:
	  case 501:
	  case 502:  /* Proxy error */
	  case 503:
	  case 504:
	  case 505:
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

	return result;
}


static int statuscolor_by_set(testedhost_t *h, long status, char *okcodes, char *badcodes)
{
	int result = -1;
	char codestr[10];
	pcre *ptn;

	/* Use code 999 to indicate we could not fetch the URL */
	sprintf(codestr, "%ld", (status ? status : 999));

	if (okcodes) {
		ptn = compileregex(okcodes);
		if (matchregex(codestr, ptn)) result = COL_GREEN; else result = COL_RED;
		freeregex(ptn);
	}

	if (badcodes) {
		ptn = compileregex(badcodes);
		if (matchregex(codestr, ptn)) result = COL_RED; else result = COL_GREEN;
		freeregex(ptn);
	}

	if (result == -1) result = statuscolor(h, status);

	dbgprintf("Host %s status %s [%s:%s] -> color %s\n", 
		  h->hostname, codestr, 
		  (okcodes ? okcodes : "<null>"),
		  (badcodes ? badcodes : "<null>"),
		  colorname(result));

	return result;
}


void send_http_results(service_t *httptest, testedhost_t *host, testitem_t *firsttest,
		       char *nonetpage, int failgoesclear, int usebackfeedqueue)
{
	testitem_t *t;
	int	color = -1;
	char    *svcname;
	strbuffer_t *msgtext;
	char    *nopagename;
	int     nopage = 0;
	int	anydown = 0, totalreports = 0;

	if (firsttest == NULL) return;

	svcname = strdup(httptest->testname);
	if (httptest->namelen) svcname[httptest->namelen] = '\0';

	/* Check if this service is a NOPAGENET service. */
	nopagename = (char *) malloc(strlen(svcname)+3);
	sprintf(nopagename, ",%s,", svcname);
	nopage = (strstr(nonetpage, svcname) != NULL);
	xfree(nopagename);

	dbgprintf("Calc http color host %s : ", host->hostname);

	msgtext = newstrbuffer(0);
	for (t=firsttest; (t && (t->host == host)); t = t->next) {
		http_data_t *req = (http_data_t *) t->privdata;

		/* Skip the data-reports for now */
		if (t->senddata) continue;

		/* Grab session cookies */
		update_session_cookies(host->hostname, req->weburl.desturl->host, req->headers);

		totalreports++;
		if (req->weburl.okcodes || req->weburl.badcodes) {
			req->httpcolor = statuscolor_by_set(host, req->httpstatus, req->weburl.okcodes, req->weburl.badcodes);
		}
		else {
			req->httpcolor = statuscolor(host, req->httpstatus);
		}
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
				req->faileddeps = strdup(faileddeps);
			}
		}

		dbgprintf("%s(%s) ", t->testspec, colorname(req->httpcolor));
		if (req->httpcolor > color) color = req->httpcolor;

		/* Build the short msgtext which goes on line 1 of the status message. */
		addtobuffer(msgtext, (STRBUFLEN(msgtext) ? " ; " : ": ") );
		if (req->tcptest->errcode != CONTEST_ENOERROR) {
			switch (req->tcptest->errcode) {
			  case CONTEST_ETIMEOUT: 
				  req->errorcause = "Server timeout"; break;
			  case CONTEST_ENOCONN : 
				  req->errorcause =  strdup(strerror(req->tcptest->connres)); break;
			  case CONTEST_EDNS    : 
				  switch (req->parsestatus) {
					  case 1 : req->errorcause =  "Invalid URL"; break;
					  case 2 : req->errorcause =  "Hostname not in DNS"; break;
					  default: req->errorcause =  "DNS error"; break;
				  }
				  break;
			  case CONTEST_EIO     : 
				  req->errorcause =  "I/O error"; break;
			  case CONTEST_ESSL    : 
				  req->errorcause =  "SSL error"; break;
			  default: 
				  req->errorcause =  "Xfer failed";
			}

			addtobuffer(msgtext, req->errorcause);
		} 
		else if (req->tcptest->open == 0) {
			req->errorcause = "Connect failed";
			addtobuffer(msgtext, req->errorcause);
		}
		else if ((req->httpcolor == COL_RED) || (req->httpcolor == COL_YELLOW)) {
			char m1[100];

			if (req->weburl.okcodes || req->weburl.badcodes) {
				sprintf(m1, "Unwanted HTTP status %ld", req->httpstatus);
			}
			else if (req->headers) {
				char *p = req->headers;

				/* Skip past "HTTP/1.x 200 " and pick up the explanatory text, if any */
				if (strncasecmp(p, "http/", 5) == 0) {
					p += 5;
					p += strspn(p, "0123456789. ");
				}

				strncpy(m1, p, sizeof(m1)-1);
				m1[sizeof(m1)-1] = '\0';

				/* Only show the first line of the HTTP status description */
				p = m1 + strcspn(m1, "\n\r"); *p = '\0';
			}
			else {
				sprintf(m1, "Connected, but got empty response (code:%ld)", req->httpstatus);
			}
			addtobuffer(msgtext, m1);
			req->errorcause = strdup(m1);
		}
		else {
			addtobuffer(msgtext, "OK");
			if (req->weburl.okcodes || req->weburl.badcodes) {
				char m1[100];

				sprintf(m1, " (HTTP status %ld)", req->httpstatus);
				addtobuffer(msgtext, m1);
			}
		}
	}

	/* It could be that we have 0 http tests - if we only do the apache one */
	if (totalreports > 0) {
		char msgline[4096];

		if (anydown) {
			firsttest->downcount++; 
			if(firsttest->downcount == 1) firsttest->downstart = getcurrenttime(NULL);
		} 
		else firsttest->downcount = 0;

		/* Handle the "badtest" stuff for http tests */
		if ((color == COL_RED) && (firsttest->downcount < firsttest->badtest[2])) {
			if      (firsttest->downcount >= firsttest->badtest[1]) color = COL_YELLOW;
			else if (firsttest->downcount >= firsttest->badtest[0]) color = COL_CLEAR;
			else                                                    color = COL_GREEN;
		}

		if (nopage && (color == COL_RED)) color = COL_YELLOW;
		dbgprintf(" --> %s\n", colorname(color));

		/* Send off the http status report */
		init_status(color);
		sprintf(msgline, "status+%d %s.%s %s %s", 
			validity, commafy(host->hostname), svcname, colorname(color), timestamp);
		addtostatus(msgline);
		addtostrstatus(msgtext);
		addtostatus("\n");

		for (t=firsttest; (t && (t->host == host)); t = t->next) {
			char *urlmsg;
			http_data_t *req = (http_data_t *) t->privdata;

			/* Skip the "data" reports */
			if (t->senddata) continue;

			urlmsg = (char *)malloc(1024 + strlen(req->url));
			sprintf(urlmsg, "\n&%s %s - ", colorname(req->httpcolor), req->url);
			addtostatus(urlmsg);

			if (req->httpcolor == COL_GREEN) addtostatus("OK");
			else {
				if (req->errorcause) addtostatus(req->errorcause);
				else addtostatus("failed");
			}
			if (req->weburl.okcodes || req->weburl.badcodes) {
				char m1[100];

				sprintf(m1, " (HTTP status %ld)", req->httpstatus);
				addtostatus(m1);
			}
			addtostatus("\n");

			if (req->headers) {
				addtostatus("\n");
				addtostatus(req->headers);
			}
			if (req->faileddeps) addtostatus(req->faileddeps);

			sprintf(urlmsg, "\nSeconds: %5d.%02d\n\n", 
				(unsigned int)req->tcptest->totaltime.tv_sec, 
				(unsigned int)req->tcptest->totaltime.tv_nsec / 10000000 );
			addtostatus(urlmsg);
			xfree(urlmsg);
		}
		addtostatus("\n\n");
		finish_status();
	}

	/* Send of any HTTP status tests in separate columns */
	for (t=firsttest; (t && (t->host == host)); t = t->next) {
		int color;
		char msgline[4096];
		char *urlmsg;
		http_data_t *req = (http_data_t *) t->privdata;

		if ((t->senddata) || (!req->weburl.columnname) || (req->contentcheck != CONTENTCHECK_NONE)) continue;

		/* Handle the "badtest" stuff */
		color = req->httpcolor;
		if ((color == COL_RED) && (t->downcount < t->badtest[2])) {
			if      (t->downcount >= t->badtest[1]) color = COL_YELLOW;
			else if (t->downcount >= t->badtest[0]) color = COL_CLEAR;
			else                                    color = COL_GREEN;
		}

		if (nopage && (color == COL_RED)) color = COL_YELLOW;

		/* Send off the http status report */
		init_status(color);
		sprintf(msgline, "status+%d %s.%s %s %s", 
			validity, commafy(host->hostname), req->weburl.columnname, colorname(color), timestamp);
		addtostatus(msgline);

		addtostatus(" : ");
		addtostatus(req->errorcause ? req->errorcause : "OK");
		if (req->weburl.okcodes || req->weburl.badcodes) {
			char m1[100];

			sprintf(m1, " (HTTP status %ld)", req->httpstatus);
			addtostatus(m1);
		}
		addtostatus("\n");

		urlmsg = (char *)malloc(1024 + strlen(req->url));
		sprintf(urlmsg, "\n&%s %s - ", colorname(req->httpcolor), req->url);
		addtostatus(urlmsg);
		xfree(urlmsg);

		if (req->httpcolor == COL_GREEN) addtostatus("OK");
		else {
			if (req->errorcause) addtostatus(req->errorcause);
			else addtostatus("failed");
		}
		addtostatus("\n");

		if (req->headers) {
			addtostatus("\n");
			addtostatus(req->headers);
		}
		if (req->faileddeps) addtostatus(req->faileddeps);

		sprintf(msgline, "\nSeconds: %5d.%02d\n\n", 
			(unsigned int)req->tcptest->totaltime.tv_sec, 
			(unsigned int)req->tcptest->totaltime.tv_nsec / 10000000 );
		addtostatus(msgline);

		addtostatus("\n\n");
		finish_status();
	}

	/* Send off any "data" messages now */
	for (t=firsttest; (t && (t->host == host)); t = t->next) {
		http_data_t *req;
		char *data = "";
		strbuffer_t *msg = newstrbuffer(0);
		char msgline[1024];

		if (!t->senddata) continue;

		req = (http_data_t *) t->privdata;
		if (req->output) data = req->output;

		sprintf(msgline, "data %s.%s\n", commafy(host->hostname), req->weburl.columnname);
		addtobuffer(msg, msgline);
		addtobuffer(msg, data);
		combo_add(msg);

		freestrbuffer(msg);
	}

	xfree(svcname);
	freestrbuffer(msgtext);
}


static testitem_t *nextcontenttest(service_t *httptest, testedhost_t *host, testitem_t *current)
{
	testitem_t *result;

	result = current->next;

	if ((result == NULL) || (result->host != host)) {
		result = NULL;
	}

	return result;
}

void send_content_results(service_t *httptest, testedhost_t *host,
			  char *nonetpage, char *contenttestname, int failgoesclear)
{
	testitem_t *t, *firsttest;
	int	color = -1;
	char    *nopagename;
	int     nopage = 0;
	char    *conttest;
	int 	contentnum = 0;
	conttest = (char *) malloc(128);

	if (host->firsthttp == NULL) return;

	/* Check if this service is a NOPAGENET service. */
	nopagename = (char *) malloc(strlen(contenttestname)+3);
	sprintf(nopagename, ",%s,", contenttestname);
	nopage = (strstr(nonetpage, contenttestname) != NULL);
	xfree(nopagename);

	dbgprintf("Calc content color host %s : ", host->hostname);

	firsttest = host->firsthttp;

	for (t=firsttest; (t && (t->host == host)); t = nextcontenttest(httptest, host, t)) {
		http_data_t *req = (http_data_t *) t->privdata;
		char cause[100];
		char *msgline;
		int got_data = 1;

		/* Skip the "data"-only messages */
		if (t->senddata) continue;
		if (!req->contentcheck) continue;

		/* We have a content check */
		strcpy(cause, "Content OK");
		if (req->contstatus == 0) {
			/* The content check passed initial checks of regexp etc. */
			color = statuscolor(t->host, req->httpstatus);
			if (color == COL_GREEN) {
				/* We got the data from the server */
				int status = 0;

				switch (req->contentcheck) {
				  case CONTENTCHECK_REGEX:
					if (req->output) {
						regmatch_t foo[1];

						status = regexec((regex_t *) req->exp, req->output, 0, foo, 0);
						if (status != 0) {
							void *hinfo = hostinfo(host->hostname);

							if (hinfo && xmh_item(hinfo, XMH_FLAG_HTTP_HEADER_MATCH)) 
								status = regexec((regex_t *) req->exp, req->headers, 0, foo, 0);
						}
						regfree((regex_t *) req->exp);
					}
					else {
						/* output may be null if we only got a redirect */
						status = STATUS_CONTENTMATCH_FAILED;
					}
					break;

				  case CONTENTCHECK_NOREGEX:
					if (req->output) {
						regmatch_t foo[1];
						void *hinfo = hostinfo(host->hostname);

						if (hinfo && xmh_item(hinfo, XMH_FLAG_HTTP_HEADER_MATCH)) {
							status = ( (!regexec((regex_t *) req->exp, req->output, 0, foo, 0)) &&
								   (!regexec((regex_t *) req->exp, req->headers, 0, foo, 0)) );
						}
						else {
							status = (!regexec((regex_t *) req->exp, req->output, 0, foo, 0));
						}
						regfree((regex_t *) req->exp);
					}
					else {
						/* output may be null if we only got a redirect */
						status = STATUS_CONTENTMATCH_FAILED;
					}
					break;

				  case CONTENTCHECK_DIGEST:
					if (req->digest == NULL) req->digest = strdup("");
					if (strcmp(req->digest, (char *)req->exp) != 0) {
						status = STATUS_CONTENTMATCH_FAILED;
					}
					else status = 0;

					req->output = (char *) malloc(strlen(req->digest)+strlen((char *)req->exp)+strlen("Expected:\nGot     :\n")+1);
					sprintf(req->output, "Expected:%s\nGot     :%s\n", 
						(char *)req->exp, req->digest);
					break;

				  case CONTENTCHECK_CONTENTTYPE:
					if (req->contenttype && (strcasecmp(req->contenttype, (char *)req->exp) == 0)) {
						status = 0;
					}
					else {
						status = STATUS_CONTENTMATCH_FAILED;
					}

					if (req->contenttype == NULL) req->contenttype = strdup("No content-type provdied");

					req->output = (char *) malloc(strlen(req->contenttype)+strlen((char *)req->exp)+strlen("Expected content-type: %s\nGot content-type     : %s\n")+1);
					sprintf(req->output, "Expected content-type: %s\nGot content-type     : %s\n",
						(char *)req->exp, req->contenttype);
					break;
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

			if (nopage && (color == COL_RED)) color = COL_YELLOW;
		}
		else {
			/* This only happens upon internal errors in Xymon test system */
			color = statuscolor(t->host, req->contstatus);
			strcpy(cause, "Internal Xymon error");
		}

		/* Send the content status message */
		dbgprintf("Content check on %s is %s\n", req->url, colorname(color));

		if (req->weburl.columnname) {
			strcpy(conttest, req->weburl.columnname);
		}
		else {
			if (contentnum > 0) sprintf(conttest, "%s%d", contenttestname, contentnum);
			else strcpy(conttest, contenttestname);

			contentnum++;
		}

		msgline = (char *)malloc(4096 + (2 * strlen(req->url)));
		init_status(color);
		sprintf(msgline, "status+%d %s.%s %s %s: %s\n", 
			validity, commafy(host->hostname), conttest, colorname(color), timestamp, cause);
		addtostatus(msgline);

		if (!got_data) {
			if (host->hidehttp) {
				sprintf(msgline, "\nContent check failed\n");
			}
			else {
				sprintf(msgline, "\nAn error occurred while testing <a href=\"%s\">URL %s</a>\n", 
					req->url, req->url);
			}
		}
		else {
			if (host->hidehttp) {
				sprintf(msgline, "\n&%s Content check %s\n",
					colorname(color), ((color == COL_GREEN) ? "OK" : "Failed"));
			}
			else {
				sprintf(msgline, "\n&%s %s - Testing <a href=\"%s\">URL</a> yields:\n",
					colorname(color), req->url, req->url);
			}
		}
		addtostatus(msgline);
		xfree(msgline);

		if (req->output == NULL) {
			addtostatus("\nNo output received from server\n\n");
		}
		else if (!host->hidehttp) {
			/* Dont flood xymond with data */
			if (req->outlen > MAX_CONTENT_DATA) {
				*(req->output + MAX_CONTENT_DATA) = '\0';
				req->outlen = MAX_CONTENT_DATA;
			}

			if ( (req->contenttype && (strncasecmp(req->contenttype, "text/html", 9) == 0)) ||
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

		addtostatus("\n\n");
		finish_status();
	}

	xfree(conttest);
}


void show_http_test_results(service_t *httptest)
{
	http_data_t *req;
	testitem_t *t;

	for (t = httptest->items; (t); t = t->next) {
		req = (http_data_t *) t->privdata;

		printf("URL                      : %s\n", req->url);
		printf("HTTP status              : %lu\n", req->httpstatus);
		printf("HTTP headers\n%s\n", textornull(req->headers));
		printf("HTTP output\n%s\n", textornull(req->output));
		printf("------------------------------------------------------\n");
	}
}

