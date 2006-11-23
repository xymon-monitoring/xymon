/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOCATOR_H__
#define __LOCATOR_H__

enum locator_servicetype_t { ST_RRD, ST_CLIENT, ST_ALERT, ST_HISTORY, ST_HOSTDATA, ST_MAX } ;
extern const char *servicetype_names[];
enum locator_sticky_t { LOC_ROAMING, LOC_STICKY, LOC_SINGLESERVER } ;

extern enum locator_servicetype_t get_servicetype(char *typestr);
extern int locator_init(char *ipport);
extern void locator_prepcache(enum locator_servicetype_t svc, int timeout);
extern void locator_flushcache(enum locator_servicetype_t svc, char *key);
extern char *locator_ping(void);
extern int locator_register_server(char *servername, enum locator_servicetype_t svctype, int weight, enum locator_sticky_t sticky, char *extras);
extern int locator_register_host(char *hostname, enum locator_servicetype_t svctype, char *servername);
extern int locator_rename_host(char *oldhostname, char *newhostname, enum locator_servicetype_t svctype);
extern char *locator_query(char *hostname, enum locator_servicetype_t svctype, char **extras);
extern int locator_serverup(char *servername, enum locator_servicetype_t svctype);
extern int locator_serverdown(char *servername, enum locator_servicetype_t svctype);

#endif

