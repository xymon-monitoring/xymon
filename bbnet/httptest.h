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

#include "bbtest-net.h"

extern void add_http_test(testitem_t *t);
extern void run_http_tests(service_t *httptest, long followlocations, char *logfile, int sslcertcheck);
extern void show_http_test_results(service_t *httptest);
extern void send_http_results(service_t *httptest, testedhost_t *host, char *nonetpage, 
		char *contenttestname, char *ssltestname, int sslwarndays, int sslalarmdays);

#endif

