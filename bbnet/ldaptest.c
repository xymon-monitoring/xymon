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

static char rcsid[] = "$Id: ldaptest.c,v 1.16 2004-08-28 07:12:09 henrik Exp $";

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>

#include "bbgen.h"
#include "util.h"
#include "sendmsg.h"
#include "debug.h"
#include "bbtest-net.h"
#include "ldaptest.h"

#define BBGEN_LDAP_OK 		0
#define BBGEN_LDAP_INITFAIL	10
#define BBGEN_LDAP_TLSFAIL	11
#define BBGEN_LDAP_BINDFAIL	20
#define BBGEN_LDAP_TIMEOUT	30
#define BBGEN_LDAP_SEARCHFAILED	40

char *ldap_library_version = NULL;

static volatile int connect_timeout = 0;

int init_ldap_library(void)
{
#ifdef BBGEN_LDAP
	char versionstring[100];

	/* Doesnt really do anything except define the version-number string */
	sprintf(versionstring, "%s %d", LDAP_VENDOR_NAME, LDAP_VENDOR_VERSION);
	ldap_library_version = malcop(versionstring);
#endif

	return 0;
}

void shutdown_ldap_library(void)
{
#ifdef BBGEN_LDAP
	/* No-op for LDAP */
#endif
}

int add_ldap_test(testitem_t *t)
{
#ifdef BBGEN_LDAP
	ldap_data_t *req;
	LDAPURLDesc *ludp;
	char *urltotest;

	/* 
	 * t->testspec containts the full testspec
	 * We need to remove any URL-encoding.
	 */
	urltotest = urlunescape(t->testspec);

	if (ldap_url_parse(urltotest, &ludp) != 0) {
		errprintf("Invalid LDAP URL %s\n", t->testspec);
		return 1;
	}

	/* Allocate the private data and initialize it */
	t->privdata = (void *) malloc(sizeof(ldap_data_t)); 
	req = (ldap_data_t *) t->privdata;
	req->ldapdesc = (void *) ludp;
	req->usetls = (strncmp(urltotest, "ldaps:", 6) == 0);
#ifdef BBGEN_LDAP_USESTARTTLS
	if (req->usetls && (ludp->lud_port == LDAPS_PORT)) {
		dprintf("Forcing port %d for ldaps with STARTTLS\n", LDAP_PORT );
		ludp->lud_port = LDAP_PORT;
	}
#endif
	req->ldapstatus = 0;
	req->output = NULL;
	req->ldapcolor = -1;
	req->faileddeps = NULL;
	req->duration.tv_sec = req->duration.tv_usec = 0;
	req->certinfo = NULL;
	req->certexpires = 0;
#endif

	return 0;
}


static void ldap_alarmhandler(int signum)
{
	signal(signum, SIG_DFL);
	connect_timeout = 1;
}

void run_ldap_tests(service_t *ldaptest, int sslcertcheck, int querytimeout)
{
#ifdef BBGEN_LDAP
	ldap_data_t *req;
	testitem_t *t;
	struct timeval starttime;
	struct timeval endtime;
	struct timezone tz;

	/* Pick a sensible default for the timeout setting */
	if (querytimeout == 0) querytimeout = 30;

	for (t = ldaptest->items; (t); t = t->next) {
		LDAPURLDesc	*ludp;
		LDAP		*ld;
		int		rc, finished;
		int		msgID = -1;
		struct timeval	timeout;
		LDAPMessage	*result;
		LDAPMessage	*e;
		char		response[MAXMSG];

		req = (ldap_data_t *) t->privdata;
		ludp = (LDAPURLDesc *) req->ldapdesc;

		gettimeofday(&starttime, &tz);

		/* Initiate session with the LDAP server */
		dprintf("Initiating LDAP session for host %s port %d\n",
			ludp->lud_host, ludp->lud_port);

		if( (ld = ldap_init(ludp->lud_host, ludp->lud_port)) == NULL ) {
			dprintf("ldap_init failed\n");
			req->ldapstatus = BBGEN_LDAP_INITFAIL;
			continue;
		}

		/* 
		 * There is apparently no standard way of defining a network
		 * timeout for the initial connection setup. OpenLDAP does
		 * have an undocumented ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv)
		 * but this is a) undocumented, and b) non-portable.
		 * 
		 * So using an alarm() to interrupt any pending operations
		 * seems to be the least insane way of doing this.
		 *
		 * Note that we must do this right after ldap_init(), as
		 * any operation on the session handle (ld) may trigger the
		 * network connection to be established.
		 */
		connect_timeout = 0;
		signal(SIGALRM, ldap_alarmhandler);
		alarm(querytimeout);

#ifdef BBGEN_LDAP_USESTARTTLS
		/*
		 * This is completely undocumented in the OpenLDAP docs.
		 *
		 * Both of these routines appear in the <ldap.h> file 
		 * from OpenLDAP 2.1.22. Their use to enable TLS has
		 * been deciphered from the ldapsearch() utility
		 * sourcecode.
		 *
		 */
		if (req->usetls) {
			int protocol = 3;

			dprintf("Attempting to select LDAPv3 for TLS\n");
			if ((rc = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &protocol)) != LDAP_OPT_SUCCESS) {
				dprintf("Failed to force protocol 3\n");
				req->output = malcop(ldap_err2string(rc));
				req->ldapstatus = BBGEN_LDAP_TLSFAIL;
				continue;
			}

			dprintf("Trying to enable TLS for session\n");
			if ((rc = ldap_start_tls_s(ld, NULL, NULL)) != LDAP_SUCCESS) {
				dprintf("ldap_start_tls failed\n");
				req->output = malcop(ldap_err2string(rc));
				req->ldapstatus = BBGEN_LDAP_TLSFAIL;
				continue;
			}
		}
#endif

		if (!connect_timeout) {
			msgID = ldap_simple_bind(ld, (t->host->ldapuser ? t->host->ldapuser : ""), 
					 (t->host->ldappasswd ? t->host->ldappasswd : ""));
		}

		/* Cancel any pending alarms */
		alarm(0);
		signal(SIGALRM, SIG_DFL);

		/* Did we connect? */
		if (connect_timeout || (msgID == -1)) {
			req->ldapstatus = BBGEN_LDAP_BINDFAIL;
			req->output = "Cannot connect to server";
			continue;
		}

		/* Wait for bind to complete */
		rc = 0; finished = 0; 
		timeout.tv_sec = querytimeout;
		timeout.tv_usec = 0L;
		while( ! finished ) {
			int rc2;

			rc = ldap_result(ld, msgID, LDAP_MSG_ONE, &timeout, &result);
			dprintf("ldap_result returned %d for ldap_simple_bind()\n", rc);
			if(rc == -1) {
				finished = 1;
				rc2 = ldap_result2error(ld, result, 1);
				req->ldapstatus = BBGEN_LDAP_BINDFAIL;
				req->output = malcop(ldap_err2string(rc2));
				ldap_unbind(ld);
			}
			if (rc == 0) {
				finished = 1;
				req->ldapstatus = BBGEN_LDAP_BINDFAIL;
				req->output = "Connection timeout";
				ldap_unbind(ld);
			}
			if( rc > 0 ) {
				finished = 1;
				if (result == NULL) {
					errprintf("LDAP library problem - got a NULL resultcode for status %d\n", rc);
					req->ldapstatus = BBGEN_LDAP_BINDFAIL;
					req->output = "LDAP library problem: ldap_result2error returned a NULL result for status %d\n";
					ldap_unbind(ld);
				}
				else {
					rc2 = ldap_result2error(ld, result, 1);
					if(rc2 != LDAP_SUCCESS) {
						req->ldapstatus = BBGEN_LDAP_BINDFAIL;
						req->output = malcop(ldap_err2string(rc));
						ldap_unbind(ld);
					}
				}
			}
		} /* ... while() */

		/* We're done connecting. If something went wrong, go to next query. */
		if (req->ldapstatus != 0) continue;

		/* Now do the search. With a timeout */
		timeout.tv_sec = querytimeout;
		timeout.tv_usec = 0L;
		rc = ldap_search_st(ld, ludp->lud_dn, ludp->lud_scope, ludp->lud_filter, ludp->lud_attrs, 0, &timeout, &result);

		if(rc == LDAP_TIMEOUT) {
			req->ldapstatus = BBGEN_LDAP_TIMEOUT;
			req->output = malcop(ldap_err2string(rc));
	  		ldap_unbind(ld);
			continue;
		}
		if( rc != LDAP_SUCCESS ) {
			req->ldapstatus = BBGEN_LDAP_SEARCHFAILED;
			req->output = malcop(ldap_err2string(rc));
	  		ldap_unbind(ld);
			continue;
		}

		gettimeofday(&endtime, &tz);

		sprintf(response, "Searching LDAP for %s yields %d results:\n\n", 
			t->testspec, ldap_count_entries(ld, result));

		for(e = ldap_first_entry(ld, result); (e != NULL); e = ldap_next_entry(ld, e) ) {
			char 		*dn;
			BerElement	*ber;
			char		*attribute;
			char		**vals;
			char		buf[MAX_LINE_LEN];

			dn = ldap_get_dn(ld, e);
			sprintf(buf, "DN: %s\n", dn); 
			strcat(response, buf);

			/* Addtributes and values */
			for (attribute = ldap_first_attribute(ld, e, &ber); (attribute != NULL); attribute = ldap_next_attribute(ld, e, ber) ) {
				if ((vals = ldap_get_values(ld, e, attribute)) != NULL) {
					int i;

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
			ldap_memfree(dn);
			if (ber != NULL) ber_free(ber, 0);

			strcat(response, "\n");
		}
		req->ldapstatus = BBGEN_LDAP_OK;
		req->output = malcop(response);
		tvdiff(&starttime, &endtime, &req->duration);

		ldap_msgfree(result);
		ldap_unbind(ld);
		ldap_free_urldesc(ludp);
	}
#endif
}


static int statuscolor(testedhost_t *host, int ldapstatus)
{
	switch (ldapstatus) {
	  case BBGEN_LDAP_OK:
		return COL_GREEN;

	  case BBGEN_LDAP_INITFAIL:
	  case BBGEN_LDAP_TLSFAIL:
	  case BBGEN_LDAP_BINDFAIL:
	  case BBGEN_LDAP_TIMEOUT:
		return COL_RED;

	  case BBGEN_LDAP_SEARCHFAILED:
		return (host->ldapsearchfailyellow ? COL_YELLOW : COL_RED);
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
	char	*svcname;

	if (ldap1 == NULL) return;

	svcname = malcop(ldaptest->testname);
	if (ldaptest->namelen) svcname[ldaptest->namelen] = '\0';

	/* Check if this service is a NOPAGENET service. */
	nopagename = (char *) malloc(strlen(svcname)+3);
	sprintf(nopagename, ",%s,", svcname);
	nopage = (strstr(nonetpage, svcname) != NULL);
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

		dprintf("%s(%s) ", t->testspec, colorname(req->ldapcolor));
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
		commafy(host->hostname), svcname, colorname(color), timestamp);
	addtostatus(msgline);

	for (t=host->firstldap; (t && (t->host == host)); t = t->next) {
		ldap_data_t *req = (ldap_data_t *) t->privdata;

		sprintf(msgline, "\n&%s %s - %s\n\n", colorname(req->ldapcolor), t->testspec,
			((req->ldapcolor != COL_GREEN) ? "failed" : "OK"));
		addtostatus(msgline);

		if (req->output) {
			addtostatus(req->output);
			addtostatus("\n\n");
		}
		if (req->faileddeps) addtostatus(req->faileddeps);

		sprintf(msgline, "\nSeconds: %u.%02u\n",
			(unsigned int)req->duration.tv_sec, (unsigned int)req->duration.tv_usec / 10000);

		addtostatus(msgline);
	}
	addtostatus("\n");
	finish_status();
	free(svcname);
}


void show_ldap_test_results(service_t *ldaptest)
{
	ldap_data_t *req;
	testitem_t *t;

	for (t = ldaptest->items; (t); t = t->next) {
		req = (ldap_data_t *) t->privdata;

		printf("URL        : %s\n", t->testspec);
		printf("Time spent : %u.%02u\n", 
			(unsigned int)req->duration.tv_sec, 
			(unsigned int)req->duration.tv_usec / 10000);
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
	int ldapdebug = 0;

	while ((argi < argc) && (strncmp(argv[argi], "--", 2) == 0)) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--ldapdebug=", strlen("--ldapdebug=")) == 0) {
			char *p = strchr(argv[argi], '=');
			ldapdebug = atoi(p+1);
		}
		argi++;
	}

	/* For testing, dont crash in sendmsg when no BBDISP defined */
	dontsendmessages = 1;
	if (getenv("BBDISP") == NULL) putenv("BBDISP=127.0.0.1");

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
	item.testspec = urlunescape(argv[argi]);

	host.firstldap = &item;
	host.hostname = "ldaptest.bbgen";
	host.ldapuser = NULL;
	host.ldappasswd = NULL;

	init_ldap_library();

	if (ldapdebug) {
#if defined(LBER_OPT_DEBUG_LEVEL) && defined(LDAP_OPT_DEBUG_LEVEL)
		ber_set_option(NULL, LBER_OPT_DEBUG_LEVEL, &ldapdebug);
		ldap_set_option(NULL, LDAP_OPT_DEBUG_LEVEL, &ldapdebug);
#else
		printf("LDAP library does not support change of debug level\n");
#endif
	}

	if (add_ldap_test(&item) == 0) {
		run_ldap_tests(&ldapservice, 0, 10);
		combo_start();
		send_ldap_results(&ldapservice, &host, "", 0);
		combo_end();
	}

	shutdown_ldap_library();
	return 0;
}

#endif

