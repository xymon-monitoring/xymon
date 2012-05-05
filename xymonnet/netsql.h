/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __NETSQL_H__
#define __NETSQL_H__

extern int xymon_sqldb_init(void);
extern void xymon_sqldb_shutdown(void);

extern void xymon_sqldb_flushall(void);

extern void xymon_sqldb_dns_updatecache(int family, char *key, char *ip);
extern int xymon_sqldb_dns_lookup_search(int family, char *key, time_t *updtime, char **res);
extern int xymon_sqldb_dns_lookup_create(int family, char *key);
extern void xymon_sqldb_dns_lookup_finish(void);

extern int xymon_sqldb_nettest_due(char *hostname, char *testspec, char *destination, net_test_options_t *options, char *location);
extern int xymon_sqldb_nettest_row(char *location, char **hostname, char **testspec, char **destination, net_test_options_t *options);
extern void xymon_sqldb_nettest_rowupdate(char *hostname, char *testspec);

#endif

