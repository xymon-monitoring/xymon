/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/* This tool implements testing of a single LDAP URL                          */
/*                                                                            */
/* Copyright (C) 2004-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <glob.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#include <lber.h>
#define LDAP_DEPRECATED 1
#include <ldap.h>

#include "libxymon.h"

int timeout = 30;

int test_ldap(char *url, char *username, char *password, strbuffer_t *outdata, int *elapsedus)
{
	LDAPURLDesc *ludp = NULL;
	LDAP *ldaphandle = NULL;
	LDAPMessage *e, *result = NULL;
	struct timespec starttime, endtime;
	struct timeval nettimeout;
	int rc, msgID = -1;
	char msgtext[4096];
	int testresult = 0;
	char *realurl = NULL;
	int usestarttls = 0;

	getntimer(&starttime);

	if (!username) username = "";
	if (!password) password = "";

	/* We use an "ldaptls://" URL to signal that we must use STARTTLS. Convert this to standard "ldap://" for the LDAP library. */
	usestarttls = (strncmp(url, "ldaptls:", 8) == 0);
	if (!usestarttls)
		realurl = strdup(url);
	else {
		realurl = (char *)malloc(strlen(url) + 1);
		sprintf(realurl, "ldap:%s", url+8);
	}

	/*
	 * Parse the LDAP URL and get and LDAP handle
	 */
	if (ldap_url_parse(realurl, &ludp) != 0) {
		sprintf(msgtext, "Cannot parse LDAP URL '%s'\n", url);
		addtobuffer(outdata, msgtext);
		testresult = 1; goto cleanup;
	}

	if ((ldaphandle = ldap_init(ludp->lud_host, ludp->lud_port)) == NULL) {
		sprintf(msgtext, "ldap_init() failed for URL '%s'\n", realurl);
		addtobuffer(outdata, msgtext);
		testresult = 1; goto cleanup;
	}

	/* 
	 * Set the timeout on network operations.
	 */
	{
		nettimeout.tv_sec = timeout;
		nettimeout.tv_usec = 0;
		if ((rc = ldap_set_option(ldaphandle, LDAP_OPT_NETWORK_TIMEOUT, &nettimeout)) != LDAP_SUCCESS) {
			sprintf(msgtext, "LDAP failed to set LDAP timeout: %s\n", ldap_err2string(rc));
		}
	}

	/*
	 * Select LDAPv3 protocol. Versions of OpenLDAP post-2005
	 * actually refuse to talk LDAPv2, but try to select v3 and
	 * then fall back to v2 if that doesn't work.
	 */
	{
		int protocol = LDAP_VERSION3;

		dbgprintf("Attempting to select LDAPv3\n");
		if ((rc = ldap_set_option(ldaphandle, LDAP_OPT_PROTOCOL_VERSION, &protocol)) != LDAP_SUCCESS) {
			dbgprintf("Failed to select LDAPv3, trying LDAPv2\n");
			protocol = LDAP_VERSION2;
			if ((rc = ldap_set_option(ldaphandle, LDAP_OPT_PROTOCOL_VERSION, &protocol)) != LDAP_SUCCESS) {
				sprintf(msgtext, "LDAP failed to select LDAP protocol, cannot connect: %s\n", ldap_err2string(rc));
				addtobuffer(outdata, msgtext);
				testresult = 1; goto cleanup;
			}
		}
	}

	/* For TLS connections, try to start the TLS session. This will trigger network-level connect. */
	if (usestarttls) {
		dbgprintf("Trying to enable TLS for querying '%s'\n", realurl);
		if ((rc = ldap_start_tls_s(ldaphandle, NULL, NULL)) != LDAP_SUCCESS) {
			sprintf(msgtext, "LDAP STARTTLS failed, connect or STARTTLS error: %s\n", ldap_err2string(rc));
			addtobuffer(outdata, msgtext);
			testresult = 2; goto cleanup;
		}
	}

	/* Bind to the server */
	msgID = ldap_simple_bind(ldaphandle, username, password);
	if (msgID == -1) {
		sprintf(msgtext, "Cannot bind to LDAP server (URL: '%s')\n", realurl);
		addtobuffer(outdata, msgtext);
		testresult = 2; goto cleanup;
	}

	/* Get the result of the bind operation */
	nettimeout.tv_sec = timeout; nettimeout.tv_usec = 0;
	switch (ldap_result(ldaphandle, msgID, LDAP_MSG_ONE, &nettimeout, &result)) {
	  case -1:
		if (result == NULL) {
			addtobuffer(outdata, "LDAP BIND failed (unknown error)\n");
		}
		else {
			rc = ldap_result2error(ldaphandle, result, 1);
			sprintf(msgtext, "LDAP BIND failed: %s\n", ldap_err2string(rc));
			addtobuffer(outdata, msgtext);
		}
		testresult = 3; goto cleanup;

	  case 0:
		addtobuffer(outdata, "LDAP BIND failed: Connection timeout\n");
		testresult = 3; goto cleanup;

	  default:
		if (result == NULL) {
		ldap_unbind(ldaphandle);
			addtobuffer(outdata, "LDAP BIND failed (unknown error)\n");
			testresult = 3; goto cleanup;
		}
		else {
			rc = ldap_result2error(ldaphandle, result, 1);
			if (rc != LDAP_SUCCESS) {
				sprintf(msgtext, "LDAP BIND failed: %s\n", ldap_err2string(rc));
				addtobuffer(outdata, msgtext);
				testresult = 3; goto cleanup;
			}
		}
		break;
	}

	/* We're bound to the LDAP server. Now do the search. */
	nettimeout.tv_sec = timeout; nettimeout.tv_usec = 0;
	rc = ldap_search_st(ldaphandle, ludp->lud_dn, ludp->lud_scope, ludp->lud_filter, ludp->lud_attrs, 0, &nettimeout, &result);
	switch (rc) {
	  case LDAP_SUCCESS:
		break;
	  case LDAP_TIMEOUT:
		sprintf(msgtext, "LDAP search timeout: %s\n", ldap_err2string(rc));
		addtobuffer(outdata, msgtext);
		testresult = 4; goto cleanup;
	  default:
		sprintf(msgtext, "LDAP search failed: %s\n", ldap_err2string(rc));
		addtobuffer(outdata, msgtext);
		testresult = 5; goto cleanup;
	}

	sprintf(msgtext, "Searching LDAP for %s yields %d results:\n\n", realurl, ldap_count_entries(ldaphandle, result));
	addtobuffer(outdata, msgtext);

	for (e = ldap_first_entry(ldaphandle, result); (e != NULL); e = ldap_next_entry(ldaphandle, e) ) {
		char *dn, *attribute, **vals;
		BerElement *ber;

		dn = ldap_get_dn(ldaphandle, e);
		sprintf(msgtext, "DN: %s\n", dn); 
		addtobuffer(outdata, msgtext);

		/* Add attributes and values */
		for (attribute = ldap_first_attribute(ldaphandle, e, &ber); (attribute != NULL); attribute = ldap_next_attribute(ldaphandle, e, ber) ) {
			if ((vals = ldap_get_values(ldaphandle, e, attribute)) != NULL) {
				int i;

				for (i = 0; (vals[i] != NULL); i++) {
					sprintf(msgtext, "\t%s: %s\n", attribute, vals[i]);
					addtobuffer(outdata, msgtext);
				}
			}

			/* Free memory used to store values */
			ldap_value_free(vals);
		}
		addtobuffer(outdata, "\n");

		/* Free memory used to store attribute */
		ldap_memfree(attribute);
		ldap_memfree(dn);
		if (ber != NULL) ber_free(ber, 0);
	}

cleanup:
	getntimer(&endtime);
	*elapsedus = ntimerus(&starttime, &endtime);

	if (result) ldap_msgfree(result);
	if (ldaphandle) ldap_unbind(ldaphandle);
	if (ludp) ldap_free_urldesc(ludp);
	if (realurl) xfree(realurl);

	return testresult;
}

int main(int argc, char **argv)
{
	char *url = NULL;
	char *username = NULL;
	char *password = NULL;
	strbuffer_t *outdata = newstrbuffer(0);
	int argi, status, elapsedus;

	libxymon_init(argv[0]);

	for (argi=1; (argi < argc); argi++) {
		if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
		else if (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=');
			timeout = atoi(p+1);
		}
		else if (*(argv[argi]) != '-') {
			if (!url) url = argv[argi];
			else if (!username) username = argv[argi];
			else if (!password) password = argv[argi];
		}
	}

	if (!url) {
		fprintf(stderr, "Usage: %s [options] URL [USERNAME PASSWORD]\n", argv[0]);
		return 1;
	}

	if (!username) username = "";
	if (!password) password = "";

	status = test_ldap(url, username, password, outdata, &elapsedus);
	fprintf((status == 0) ? stdout : stderr, "%s", STRBUF(outdata));

	return status;
}

