/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the testing of an LDAP service.                  */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: ldaptest.c,v 1.3 2003-08-29 21:48:11 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "bbgen.h"
#include "util.h"
#include "sendmsg.h"
#include "debug.h"
#include "bbtest-net.h"
#include "ldaptest.h"

#define BBGEN_LDAP_OK 		0
#define BBGEN_LDAP_INITFAIL	1
#define BBGEN_LDAP_BINDFAIL	2
#define BBGEN_LDAP_TIMEOUT	3
#define BBGEN_LDAP_SEARCHFAILED	4

char *ldap_library_version = NULL;

int init_ldap_library(void)
{
#ifdef BBGEN_LDAP
	char versionstring[100];

	/* Doesnt really do anything except define the version-number string */
	sprintf(versionstring, "%s %d", LDAP_VENDOR_NAME, LDAP_VENDOR_VERSION);
	ldap_library_version = malcop(versionstring);
	return 0;
#endif
}

void shutdown_ldap_library(void)
{
#ifdef BBGEN_LDAP
	/* No-op for LDAP */
#endif
}

void add_ldap_test(testitem_t *t)
{
#ifdef BBGEN_LDAP
	ldap_data_t *req;
	LDAPURLDesc *ludp;
	char *p;

	/* 
	 * t->testspec containts the full testspec
	 */

	if (ldap_url_parse(t->testspec, &ludp) != 0) {
		errprintf("Invalid LDAP URL %s\n", t->testspec);
		return;
	}

	/* Allocate the private data and initialize it */
	req = (ldap_data_t *) malloc(sizeof(ldap_data_t));
	t->privdata = (void *) req;

	req->url = malcop(t->testspec);
	for (p=req->url; (*p); p++) if (*p == '+') *p = ' ';

	/* req->scheme = malcop(ludp->lud_scheme); -- not available on Solaris' LDAP */
	req->ldaphost = malcop(ludp->lud_host);
	req->portnumber = ludp->lud_port;

	ldap_free_urldesc(ludp);

	req->ldapstatus = 0;
	req->output = NULL;
	req->ldapcolor = -1;
	req->faileddeps = NULL;
	req->duration.tv_sec = req->duration.tv_usec = 0;
	req->certinfo = NULL;
	req->certexpires = 0;
#endif
}


void run_ldap_tests(service_t *ldaptest, int sslcertcheck)
{
#ifdef BBGEN_LDAP
	ldap_data_t *req;
	testitem_t *t;
	struct timeval starttime;
	struct timeval endtime;
	struct timezone tz;

	for (t = ldaptest->items; (t); t = t->next) {
		LDAP		*ld;
		int		rc, msgID, i, rc2;
		LDAPMessage	*result;
		LDAPMessage	*e;
		BerElement	*ber;
		char		*attribute;
		char		**vals;
		struct timeval	timeout;
		int		finished;
		char		response[MAXMSG];
		char		buf[MAX_LINE_LEN];

		req = (ldap_data_t *) t->privdata;

		gettimeofday(&starttime, &tz);

		/* Initiate session with the LDAP server */
		if( (ld = ldap_init(req->ldaphost, req->portnumber)) == NULL ) {
			dprintf("ldap_init failed\n");
			req->ldapstatus = BBGEN_LDAP_INITFAIL;
			break;
		}

		/* Bind to the server - we do an anonymous bind, asynchronous */
		msgID = ldap_simple_bind(ld, "", "");

		/* Wait for bind to complete */
		rc = 0; finished = 0; 
		timeout.tv_sec = (t->host->conntimeout ? t->host->conntimeout : DEF_CONNECT_TIMEOUT);
		timeout.tv_usec = 0L;
		while( ! finished ) {
			rc = ldap_result(ld, msgID, LDAP_MSG_ONE, &timeout, &result);
			if(rc == -1) {
				finished = 1;
				rc2 = ldap_result2error(ld, result, 1);
				req->ldapstatus = BBGEN_LDAP_BINDFAIL;
				req->output = malcop(ldap_err2string(rc2));
				ldap_unbind(ld);
				break;
			}
			if( rc >= 0 ) {
				finished = 1;
				if (result == NULL) {
					errprintf("LDAP library problem\n");
				}
				else {
					rc2 = ldap_result2error(ld, result, 1);
					if(rc2 != LDAP_SUCCESS) {
						req->ldapstatus = BBGEN_LDAP_BINDFAIL;
						req->output = malcop(ldap_err2string(rc));
						ldap_unbind(ld);
						break;
					}
				}
			}
		}

		/* Now do the search. With a timeout again */
		timeout.tv_sec = (t->host->timeout ? t->host->timeout : DEF_TIMEOUT);
		timeout.tv_usec = 0L;
		rc = ldap_url_search_st(ld, req->url, 0, &timeout, &result);
		if(rc == LDAP_TIMEOUT) {
			req->ldapstatus = BBGEN_LDAP_TIMEOUT;
			req->output = malcop(ldap_err2string(rc));
	  		ldap_unbind(ld);
			break;
		}
		if( rc != LDAP_SUCCESS ) {
			req->ldapstatus = BBGEN_LDAP_SEARCHFAILED;
			req->output = malcop(ldap_err2string(rc));
	  		ldap_unbind(ld);
			break;
		}

		gettimeofday(&endtime, &tz);

		sprintf(response, "Searching LDAP for %s yields %d results:\n\n", 
			req->url, ldap_count_entries(ld, result));
		for(e = ldap_first_entry(ld, result); (e != NULL); e = ldap_next_entry(ld, e) ) {
			sprintf(buf, "DN: %s\n", ldap_get_dn(ld, e)); strcat(response, buf);

			/* Addtributes and values */
			for (attribute = ldap_first_attribute(ld, e, &ber); (attribute != NULL); attribute = ldap_next_attribute(ld, e, ber) ) {
				if ((vals = ldap_get_values(ld, e, attribute)) != NULL) {
					for(i = 0; (vals[i] != NULL); i++) {
						sprintf(buf, "\t%s: %s\n", attribute, vals[i]);
						strcat(response, buf);
					}
				}
				/* Free memory used to store values */
				ldap_value_free(vals);
			}

			/* Free memory used to store attribute */
			ldap_memfree(attribute);

			if (ber != NULL) ber_free(ber, 0);
			strcat(response, "\n");
		}
		req->ldapstatus = BBGEN_LDAP_OK;
		req->output = malcop(response);
		req->duration.tv_sec = endtime.tv_sec - starttime.tv_sec;
		req->duration.tv_usec = endtime.tv_usec - starttime.tv_usec;
		if (req->duration.tv_usec < 0) {
			req->duration.tv_sec--;
			req->duration.tv_usec += 1000000;
		}

		ldap_msgfree(result);
		ldap_unbind( ld );
	}
#endif
}


static int statuscolor(testedhost_t *host, int ldapstatus)
{
	switch (ldapstatus) {
	  case BBGEN_LDAP_OK:
		return COL_GREEN;

	  case BBGEN_LDAP_INITFAIL:
	  case BBGEN_LDAP_BINDFAIL:
		return COL_RED;

	  case BBGEN_LDAP_TIMEOUT:
	  case BBGEN_LDAP_SEARCHFAILED:
		return COL_YELLOW;
	}

	errprintf("Unknown ldapstaus value %d\n", ldapstatus);
	return COL_RED;
}

void send_ldap_results(service_t *ldaptest, testedhost_t *host, char *nonetpage, int failgoesclear)
{
	testitem_t *t;
	int	color = -1;
	char	msgline[MAXMSG];
	char    *nopagename;
	int     nopage = 0;
	testitem_t *ldap1 = host->firstldap;
	int	anydown = 0;

	if (ldap1 == NULL) return;

	/* Check if this service is a NOPAGENET service. */
	nopagename = (char *) malloc(strlen(ldaptest->testname)+3);
	sprintf(nopagename, ",%s,", ldaptest->testname);
	nopage = (strstr(nonetpage, ldaptest->testname) != NULL);
	free(nopagename);

	dprintf("Calc ldap color host %s : ", host->hostname);
	for (t=host->firstldap; (t && (t->host == host)); t = t->next) {
		ldap_data_t *req = (ldap_data_t *) t->privdata;

		req->ldapcolor = statuscolor(host, req->ldapstatus);
		if (req->ldapcolor == COL_RED) anydown++;

		/* Dialup hosts and dialup tests report red as clear */
		if ((req->ldapcolor != COL_GREEN) && (host->dialup || t->dialup)) req->ldapcolor = COL_CLEAR;

		/* If ping failed, report CLEAR unless alwaystrue */
		if ( ((req->ldapcolor == COL_RED) || (req->ldapcolor == COL_YELLOW)) && /* Test failed */
		     (host->downcount > 0)                   && /* The ping check did fail */
		     (!host->noping && !host->noconn)        && /* We are doing a ping test */
		     (failgoesclear)                         &&
		     (!t->alwaystrue)                           )  /* No "~testname" flag */ {
			req->ldapcolor = COL_CLEAR;
		}

		/* If test we depend on has failed, report CLEAR unless alwaystrue */
		if ( ((req->ldapcolor == COL_RED) || (req->ldapcolor == COL_YELLOW)) && /* Test failed */
		      failgoesclear && !t->alwaystrue )  /* No "~testname" flag */ {
			char *faileddeps = deptest_failed(host, t->service->testname);

			if (faileddeps) {
				req->ldapcolor = COL_CLEAR;
				req->faileddeps = malcop(faileddeps);
			}
		}

		dprintf("%s(%s) ", req->url, colorname(req->ldapcolor));
		if (req->ldapcolor > color) color = req->ldapcolor;
	}

	if (anydown) ldap1->downcount++; else ldap1->downcount = 0;

	/* Handle the "badtest" stuff for ldap tests */
	if ((color == COL_RED) && (ldap1->downcount < ldap1->badtest[2])) {
		if      (ldap1->downcount >= ldap1->badtest[1]) color = COL_YELLOW;
		else if (ldap1->downcount >= ldap1->badtest[0]) color = COL_CLEAR;
		else                                            color = COL_GREEN;
	}

	if (nopage && (color == COL_RED)) color = COL_YELLOW;
	dprintf(" --> %s\n", colorname(color));

	/* Send off the ldap status report */
	init_status(color);
	sprintf(msgline, "status %s.%s %s %s", 
		commafy(host->hostname), ldaptest->testname, colorname(color), timestamp);
	addtostatus(msgline);

	for (t=host->firstldap; (t && (t->host == host)); t = t->next) {
		ldap_data_t *req = (ldap_data_t *) t->privdata;

		sprintf(msgline, "\n&%s %s - %s\n\n", colorname(req->ldapcolor), req->url,
			((req->ldapcolor != COL_GREEN) ? "failed" : "OK"));
		addtostatus(msgline);

		if (req->output) {
			addtostatus(req->output);
			addtostatus("\n\n");
		}
		if (req->faileddeps) addtostatus(req->faileddeps);

		sprintf(msgline, "\nSeconds: %ld.%03ld\n",
			req->duration.tv_sec, req->duration.tv_usec / 1000);

		addtostatus(msgline);
	}
	addtostatus("\n");
	finish_status();
}


void show_ldap_test_results(service_t *ldaptest)
{
	ldap_data_t *req;
	testitem_t *t;

	for (t = ldaptest->items; (t); t = t->next) {
		req = (ldap_data_t *) t->privdata;

		printf("URL        : %s\n", req->url);
		printf("Time spent : %ld.%03ld\n", req->duration.tv_sec, req->duration.tv_usec / 1000);
		printf("LDAP output:\n%s\n", textornull(req->output));
		printf("------------------------------------------------------\n");
	}
}


#ifdef STANDALONE

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

char *deptest_failed(testedhost_t *host, char *testname)
{
	return NULL;
}

int main(int argc, char *argv[])
{
	testitem_t item;
	testedhost_t host;
	service_t ldapservice;
	int argi = 1;

	if ((argc > 1) && (strcmp(argv[argi], "--debug") == 0)) {
		dontsendmessages = debug = 1;
		if (getenv("BBDISP") == NULL) putenv("BBDISP=127.0.0.1");
		argi++;
	}

	memset(&item, 0, sizeof(item));
	memset(&host, 0, sizeof(host));
	memset(&ldapservice, 0, sizeof(ldapservice));

	ldapservice.portnum = 389;
	ldapservice.testname = "ldap";
	ldapservice.namelen = strlen(ldapservice.testname);
	ldapservice.items = &item;

	item.host = &host;
	item.service = &ldapservice;
	item.dialup = item.reverse = item.silenttest = item.alwaystrue = 0;
	item.testspec = argv[argi];

	host.firstldap = &item;
	host.conntimeout = 5;
	host.timeout = 10;
	host.hostname = "ldaptest.bbgen";

	init_ldap_library();
	add_ldap_test(&item);
	run_ldap_tests(&ldapservice, 0);
	combo_start();
	send_ldap_results(&ldapservice, &host, "", 0);
	combo_end();

	shutdown_ldap_library();
	return 0;
}

#endif

