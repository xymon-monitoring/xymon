/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* This is used to implement the testing of a LDAP service.                   */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LDAPTEST_H_
#define __LDAPTEST_H_

#include <time.h>

#ifdef XYMON_LDAP
#include <lber.h>
#define LDAP_DEPRECATED 1
#include <ldap.h>

#ifndef LDAPS_PORT
#define LDAPS_PORT 636
#endif

#endif

typedef struct {
	void   *ldapdesc;		/* Result from ldap_url_parse() */
	int    usetls;

	int    skiptest;		/* Skip check if failed TCP connect */
	int    ldapstatus;		/* Status from library of the ldap transaction */
	char   *output;                 /* Output from ldap query */

	int    ldapcolor;		/* Final color reported */
	char   *faileddeps;
	struct timespec duration;

	char   *certinfo;               /* Data about SSL certificate */
	time_t certexpires;             /* Expiry time for SSL cert */
	int    mincipherbits;
} ldap_data_t;

extern char *ldap_library_version;
extern int  init_ldap_library(void);
extern void shutdown_ldap_library(void);

extern int  add_ldap_test(testitem_t *t);
extern void run_ldap_tests(service_t *ldaptest, int sslcertcheck, int timeout);
extern void show_ldap_test_results(service_t *ldaptest);
extern void send_ldap_results(service_t *ldaptest, testedhost_t *host, char *nonetpage, int failgoesclear);

#endif

