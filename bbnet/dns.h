/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DNS_H__
#define __DNS_H__

#include <stdio.h>

/* dnslookup values */
#define DNS_THEN_IP     0	/* Try DNS - if it fails, use IP from bb-hosts */
#define DNS_ONLY        1	/* DNS only - if it fails, report service down */
#define IP_ONLY         2	/* IP only - dont do DNS lookups */

extern int use_ares_lookup;
extern int max_dns_per_run;
extern int dnstimeout;

extern int dns_stats_total;
extern int dns_stats_success;
extern int dns_stats_failed;
extern int dns_stats_lookups;

extern FILE *dnsfaillog;

extern void add_host_to_dns_queue(char *hostname);
extern void add_url_to_dns_queue(char *hostname);
extern void flush_dnsqueue(void);
extern char *dnsresolve(char *hostname);
extern int dns_test_server(char *serverip, char *hostname, strbuffer_t *banner);

#endif

