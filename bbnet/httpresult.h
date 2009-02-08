/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* This is used to implement the testing of a HTTP service.                   */
/*                                                                            */
/* Copyright (C) 2003-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HTTPRESULT_H_
#define __HTTPRESULT_H_

#include <sys/types.h>
#include <time.h>
#include <regex.h>

extern void show_http_test_results(service_t *httptest);
extern void send_http_results(service_t *httptest, testedhost_t *host, testitem_t *firsttest,
			      char *nonetpage, int failgoesclear);
extern void send_content_results(service_t *httptest, testedhost_t *host,
				 char *nonetpage, char *contenttestname, int failgoesclear);

#endif

