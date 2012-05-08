/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
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
#include <time.h>

/* For AF_* declarations */
#include <sys/socket.h>
#include <netinet/in.h>

#include <sqlite3.h>

#include "libxymon.h"
#include "tcptalk.h"
#include "netsql.h"

static sqlite3 *xymonsqldb = NULL;

static sqlite3_stmt *dns_addrecord_sql = NULL;
static sqlite3_stmt *dns_ip4query_sql = NULL;
static sqlite3_stmt *dns_ip6query_sql = NULL;
static sqlite3_stmt *dns_ip4update_sql = NULL;
static sqlite3_stmt *dns_ip6update_sql = NULL;

static sqlite3_stmt *nettest_query_sql = NULL;
static sqlite3_stmt *nettest_addrecord_sql = NULL;
static sqlite3_stmt *nettest_updaterecord_sql = NULL;
static sqlite3_stmt *nettest_due_sql = NULL;
static sqlite3_stmt *nettest_timestamp_sql = NULL;
static sqlite3_stmt *nettest_forcetest_sql = NULL;
static sqlite3_stmt *nettest_secstonext_sql = NULL;

static sqlite3_stmt *netmodule_additem_sql = NULL;
static sqlite3_stmt *netmodule_due_sql = NULL;
static sqlite3_stmt *netmodule_purge_sql = NULL;


int xymon_sqldb_init(void)
{
	char *sqlfn;
	int dbres;

	sqlfn = (char *)malloc(strlen(xgetenv("XYMONTMP")) + strlen("/xymon.sqlite3") + 1);
	sprintf(sqlfn, "%s/xymon.sqlite3", xgetenv("XYMONTMP"));
	dbres = sqlite3_open_v2(sqlfn, &xymonsqldb, SQLITE_OPEN_READWRITE, NULL);
	if (dbres != SQLITE_OK) {
		/* Try creating the database - in that case, we must also create the tables */
		dbres = sqlite3_open_v2(sqlfn, &xymonsqldb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
		if (dbres == SQLITE_OK) {
			char *err = NULL;
			dbres = sqlite3_exec(xymonsqldb, "CREATE TABLE hostip (hostname varchar(200) unique, ip4 varchar(15), upd4time int, ip6 varchar(40), upd6time int)", NULL, NULL, &err);
			if (dbres != SQLITE_OK) {
				errprintf("Cannot create hostip table: %s\n", sqlfn, (err ? err : sqlite3_errmsg(xymonsqldb)));
				if (err) sqlite3_free(err);
				xfree(sqlfn);
				return 1;
			}
			dbres = sqlite3_exec(xymonsqldb, "CREATE UNIQUE INDEX hostip_idx ON hostip(hostname)", NULL, NULL, &err);
			if (dbres != SQLITE_OK) {
				errprintf("Cannot create hostip index: %s\n", sqlfn, (err ? err : sqlite3_errmsg(xymonsqldb)));
				if (err) sqlite3_free(err);
				xfree(sqlfn);
				return 1;
			}

			dbres = sqlite3_exec(xymonsqldb, "CREATE TABLE testtimes (hostname varchar(200), testspec varchar(400), location varchar(50), destination varchar(200), timestamp int, sourceip varchar(40), interval int, timeout int, testtype int, valid int)", NULL, NULL, &err);
			if (dbres != SQLITE_OK) {
				errprintf("Cannot create testtimes table: %s\n", sqlfn, (err ? err : sqlite3_errmsg(xymonsqldb)));
				if (err) sqlite3_free(err);
				xfree(sqlfn);
				return 1;
			}
			dbres = sqlite3_exec(xymonsqldb, "CREATE UNIQUE INDEX testtimes_idx ON testtimes(hostname,testspec,destination)", NULL, NULL, &err);
			if (dbres != SQLITE_OK) {
				errprintf("Cannot create testtimes index: %s\n", sqlfn, (err ? err : sqlite3_errmsg(xymonsqldb)));
				if (err) sqlite3_free(err);
				xfree(sqlfn);
				return 1;
			}

			dbres = sqlite3_exec(xymonsqldb, "CREATE TABLE moduletests (moduleid varchar(30), location varchar(50), hostname varchar(200), destinationip varchar(40), testspec varchar(400), extras varchar(100))", NULL, NULL, &err);
			if (dbres != SQLITE_OK) {
				errprintf("Cannot create moduletests table: %s\n", sqlfn, (err ? err : sqlite3_errmsg(xymonsqldb)));
				if (err) sqlite3_free(err);
				xfree(sqlfn);
				return 1;
			}
		}
	}
	if (dbres != SQLITE_OK) {
		errprintf("Cannot open sqlite3 database %s: %s\n", sqlfn, sqlite3_errmsg(xymonsqldb));
		xfree(sqlfn);
		return 1;
	}
	xfree(sqlfn);

	dbres = sqlite3_exec(xymonsqldb, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("Cannot set async mode: %s\n", sqlite3_errmsg(xymonsqldb));
	}

	/*
	 * testtimes table uses the tuple (hostname,testspec,destination) as a unique key.
	 * In most cases that will be (hostname,testspec,'') since 'destination' is empty, 
	 * except for "conn=..." tests that have multiple config-defined destination-IP's.
	 */

	return 0;
}

void xymon_sqldb_shutdown(void)
{
	if (dns_ip4update_sql) sqlite3_finalize(dns_ip4update_sql);
	if (dns_ip6update_sql) sqlite3_finalize(dns_ip6update_sql);
	if (dns_addrecord_sql) sqlite3_finalize(dns_addrecord_sql);
	if (dns_ip4query_sql) sqlite3_finalize(dns_ip4query_sql);
	if (dns_ip6query_sql) sqlite3_finalize(dns_ip6query_sql);

	if (nettest_query_sql) sqlite3_finalize(nettest_query_sql);
	if (nettest_addrecord_sql) sqlite3_finalize(nettest_addrecord_sql);
	if (nettest_updaterecord_sql) sqlite3_finalize(nettest_updaterecord_sql);
	if (nettest_due_sql) sqlite3_finalize(nettest_due_sql);
	if (nettest_timestamp_sql) sqlite3_finalize(nettest_timestamp_sql);
	if (nettest_forcetest_sql) sqlite3_finalize(nettest_forcetest_sql);
	if (nettest_secstonext_sql) sqlite3_finalize(nettest_secstonext_sql);

	if (netmodule_additem_sql) sqlite3_finalize(netmodule_additem_sql);
	if (netmodule_due_sql) sqlite3_finalize(netmodule_due_sql);
	if (netmodule_purge_sql) sqlite3_finalize(netmodule_purge_sql);

	if (xymonsqldb != NULL) sqlite3_close(xymonsqldb);
}


void xymon_sqldb_flushall(void)
{
	int dbres;

	dbres = sqlite3_exec(xymonsqldb, "delete from hostip", NULL, NULL, NULL);
	dbres = sqlite3_exec(xymonsqldb, "delete from testtimes", NULL, NULL, NULL);
}


void xymon_sqldb_dns_updatecache(int family, char *key, char *ip)
{
	sqlite3_stmt *updstmt = NULL;
	int dbres;

	if (!dns_ip4update_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "update hostip set ip4=?,upd4time=strftime('%s','now') where hostname=?", -1, &dns_ip4update_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("ip4update prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	if (!dns_ip6update_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "update hostip set ip6=?,upd6time=strftime('%s','now') where hostname=?", -1, &dns_ip6update_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("ip6update prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	dbgprintf("Updating cache info for %s\n", key);

	switch (family) {
	  case AF_INET: updstmt = dns_ip4update_sql; break;
	  case AF_INET6: updstmt = dns_ip6update_sql; break;
	}

	/* We know the cache record exists */
	dbres = sqlite3_bind_text(updstmt, 2, key, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(updstmt, 1, ((ip && *ip) ? ip : "-"), -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(updstmt);
	if (dbres != SQLITE_DONE) errprintf("Error updating DNS-cache record %s (%d): %s\n", key, family, sqlite3_errmsg(xymonsqldb));

	sqlite3_reset(updstmt);
}

static sqlite3_stmt *xymon_sqldb_dns_lookup_search_stmt = NULL;

int xymon_sqldb_dns_lookup_search(int family, char *key, time_t *updtime, char **res)
{
	int dbres;

	if (!dns_ip4query_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "select ip4,upd4time from hostip where hostname=?", -1, &dns_ip4query_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("ip4query prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return 1;
		}
	}

	if (!dns_ip6query_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "select ip6,upd6time from hostip where hostname=?", -1, &dns_ip6query_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("ip6query prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return 1;
		}
	}

	*res = NULL;

	switch (family) {
	  case AF_INET: xymon_sqldb_dns_lookup_search_stmt = dns_ip4query_sql; break;
	  case AF_INET6: xymon_sqldb_dns_lookup_search_stmt = dns_ip6query_sql; break;
	  default: return 1;
	}

	/* Find the cache-record */
	dbres = sqlite3_bind_text(xymon_sqldb_dns_lookup_search_stmt, 1, key, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(xymon_sqldb_dns_lookup_search_stmt);
	if (dbres == SQLITE_ROW) {
		/* See what the current cache-data is */
		*res = sqlite3_column_text(xymon_sqldb_dns_lookup_search_stmt, 0);
		*updtime = sqlite3_column_int(xymon_sqldb_dns_lookup_search_stmt, 1);
	}

	return 0;
}

void xymon_sqldb_dns_lookup_finish(void)
{
	sqlite3_reset(xymon_sqldb_dns_lookup_search_stmt);
	xymon_sqldb_dns_lookup_search_stmt = NULL;
}

int xymon_sqldb_dns_lookup_create(int family, char *key)
{
	int dbres;

	if (!dns_addrecord_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "insert into hostip(hostname,ip4,ip6,upd4time,upd6time) values (?,'','',0,0)", -1, &dns_addrecord_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("addrecord prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return 1;
		}
	}

	/* Create cache record */
	dbres = sqlite3_bind_text(dns_addrecord_sql, 1, key, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(dns_addrecord_sql);
	if ((dbres != SQLITE_DONE) && (dbres != SQLITE_CONSTRAINT)) {
		/*
		 * This can fail with a "constraint" error when there are multiple
		 * tests involving the same hostname - in that case, we may call the
		 * lookup routine multiple times before the cache record is created in
		 * the database. Therefore only show it when debugging.
		 */
		errprintf("Error adding record for %s (%d): %s\n", key, family, sqlite3_errmsg(xymonsqldb));
	}
	else {
		dbgprintf("Successfully added record for %s (%d)\n", key, family);
	}
	sqlite3_reset(dns_addrecord_sql);

	return 0;
}


/* ----------------------------------- testtimes routines ----------------------------*/

void xymon_sqldb_nettest_delete_old(int finalstep)
{
	/* Flag all records as invalid (step 1), and delete all invalid records (step 2) */
	int dbres;

	if (!finalstep)
		dbres = sqlite3_exec(xymonsqldb, "update testtimes set valid=0", NULL, NULL, NULL);
	else
		dbres = sqlite3_exec(xymonsqldb, "delete from testtimes where valid=0", NULL, NULL, NULL);

	if ((dbres != SQLITE_OK) && (dbres != SQLITE_DONE))
		errprintf("Error in bulk-update of nettest step %d: %s\n", finalstep, sqlite3_errmsg(xymonsqldb));
}


void xymon_sqldb_nettest_register(char *hostname, char *testspec, char *destination, net_test_options_t *options, char *location)
{
	/* Establish a record which is valid. If record exists, dont update timestamp - if no record, then set timestamp to 0 */
	int dbres;

	if (!nettest_query_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "select valid from testtimes where hostname=? and testspec=? and destination=?", -1, &nettest_query_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_query prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	if (!nettest_updaterecord_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "update testtimes set location=?,testtype=?,sourceip=?,timeout=?,interval=?,valid=1 where hostname=? and testspec=? and destination=?", -1, &nettest_updaterecord_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_updaterecord prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	if (!nettest_addrecord_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "insert into testtimes(hostname,testspec,location,destination,testtype,sourceip,timeout,interval,timestamp,valid) values (?,?,?,?,?,?,?,?,0,1)", -1, &nettest_addrecord_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_addrecord prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	dbres = sqlite3_bind_text(nettest_query_sql, 1, hostname, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_query_sql, 2, testspec, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_query_sql, 3, (destination ? destination : ""), -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(nettest_query_sql);
	if (dbres == SQLITE_ROW) {
		/* Record exists, so make sure it is valid */
		dbres = sqlite3_bind_text(nettest_updaterecord_sql, 1, (location ? location : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_updaterecord_sql, 2, options->testtype);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_updaterecord_sql, 3, (options->sourceip ? options->sourceip : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_updaterecord_sql, 4, options->timeout);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_updaterecord_sql, 5, options->interval);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_updaterecord_sql, 6, hostname, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_updaterecord_sql, 7, testspec, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_updaterecord_sql, 8, (destination ? destination : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_step(nettest_updaterecord_sql);

		sqlite3_reset(nettest_updaterecord_sql);
	}
	else if (dbres == SQLITE_DONE) {
		/* No record - create a new one */
		dbres = sqlite3_bind_text(nettest_addrecord_sql, 1, hostname, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_addrecord_sql, 2, testspec, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_addrecord_sql, 3, (location ? location : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_addrecord_sql, 4, (destination ? destination : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_addrecord_sql, 5, options->testtype);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_addrecord_sql, 6, (options->sourceip ? options->sourceip : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_addrecord_sql, 7, options->timeout);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_addrecord_sql, 8, options->interval);
		if (dbres == SQLITE_OK) dbres = sqlite3_step(nettest_addrecord_sql);
		if (dbres != SQLITE_DONE) errprintf("Error adding nettest-record for %s/%s: %s\n", hostname, testspec, sqlite3_errmsg(xymonsqldb));

		sqlite3_reset(nettest_addrecord_sql);
	}
	else {
		errprintf("Error querying the testtimes table: %s\n", sqlite3_errmsg(xymonsqldb));
	}

	sqlite3_reset(nettest_query_sql);
}

int xymon_sqldb_nettest_row(char *location, char **hostname, char **testspec, char **destination, net_test_options_t *options)
{
	/* Search testtimes for tests that are due to run. Return one row per invocation */

	static int inprogress = 0;
	int dbres, result = 0;
	time_t now = getcurrenttime(NULL);

	if (!nettest_due_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "select hostname,testspec,destination,testtype,sourceip,timeout from testtimes where location=? and (timestamp+interval)<?", -1, &nettest_due_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_due prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return 0;
		}
	}

	if (!inprogress) {
		dbres = sqlite3_bind_text(nettest_due_sql, 1, (location ? location : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_due_sql, 2, (int)now);
		if (dbres != SQLITE_OK) return 0;
		inprogress = 1;
	}

	dbres = sqlite3_step(nettest_due_sql);
	if (dbres == SQLITE_ROW) {
		char *srcip;
		*hostname = sqlite3_column_text(nettest_due_sql, 0);
		*testspec = sqlite3_column_text(nettest_due_sql, 1);
		*destination = sqlite3_column_text(nettest_due_sql, 2);
		options->testtype = sqlite3_column_int(nettest_due_sql, 3);
		srcip = sqlite3_column_text(nettest_due_sql, 4);
		options->sourceip = (!srcip || strlen(srcip) == 0) ? NULL : strdup(srcip);
		options->timeout = sqlite3_column_int(nettest_due_sql, 5);
		result = 1;
	}
	else {
		/* Done - no more tests */
		sqlite3_reset(nettest_due_sql);
		inprogress = 0;
	}

	return result;
}

void xymon_sqldb_nettest_forcetest(char *hostname)
{
	int dbres;

	if (!nettest_forcetest_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "update testtimes set timestamp=0 where hostname=?", -1, &nettest_forcetest_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_forcetest prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	dbres = sqlite3_bind_text(nettest_forcetest_sql, 1, hostname, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(nettest_forcetest_sql);
	if (dbres != SQLITE_DONE) errprintf("Error updating nettest-record for %s: %s\n", hostname, sqlite3_errmsg(xymonsqldb));

	sqlite3_reset(nettest_forcetest_sql);
}

void xymon_sqldb_nettest_done(char *hostname, char *testspec, char *destination)
{
	int dbres;

	if (!nettest_timestamp_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "update testtimes set timestamp=strftime('%s','now') where hostname=? and testspec=? and destination=?", -1, &nettest_timestamp_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_timestamp prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	dbres = sqlite3_bind_text(nettest_timestamp_sql, 1, hostname, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_timestamp_sql, 2, testspec, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(nettest_timestamp_sql, 3, (destination ? destination : ""), -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(nettest_timestamp_sql);
	if (dbres != SQLITE_DONE) errprintf("Error updating nettest-record for %s/%s: %s\n", hostname, testspec, sqlite3_errmsg(xymonsqldb));

	sqlite3_reset(nettest_timestamp_sql);
}

int xymon_sqldb_secs_to_next_test(void)
{
	int dbres, result;

	if (!nettest_secstonext_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "select distinct timestamp+interval-strftime('%s','now') from testtimes order by timestamp+interval limit 1", -1, &nettest_secstonext_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_secstonext prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return 60;
		}
	}

	dbres = sqlite3_step(nettest_secstonext_sql);
	if (dbres == SQLITE_ROW) {
		result = sqlite3_column_int(nettest_secstonext_sql, 0);
	}
	else {
		result = 60;
		if (dbres != SQLITE_DONE) errprintf("Error searching for seconds to next test: %s\n", sqlite3_errmsg(xymonsqldb));
	}

	sqlite3_reset(nettest_secstonext_sql);

	return result;
}


void xymon_sqldb_netmodule_additem(char *moduleid, char *location, char *hostname, char *destinationip, char *testspec, char *extras)
{
	int dbres;

	if (!netmodule_additem_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "insert into moduletests(moduleid,location,hostname,destinationip,testspec,extras) values (?,?,?,?,?,?)", -1, &netmodule_additem_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("nettest_netmodule_additem prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return;
		}
	}

	dbres = sqlite3_bind_text(netmodule_additem_sql, 1, moduleid, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(netmodule_additem_sql, 2, (location ? location : ""), -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(netmodule_additem_sql, 3, hostname, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(netmodule_additem_sql, 4, destinationip, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(netmodule_additem_sql, 5, testspec, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(netmodule_additem_sql, 6, (extras ? extras : ""), -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(netmodule_additem_sql);
	if (dbres != SQLITE_DONE) errprintf("Error adding nettest-module record for %s/%s/%s: %s\n", moduleid, hostname, testspec, sqlite3_errmsg(xymonsqldb));

	sqlite3_reset(netmodule_additem_sql);
}

int xymon_sqldb_netmodule_row(char *module, char *location, char **hostname, char **testspec, char **destination, char **extras)
{
	/* Search testtimes for tests that are due to run in a module. Return one row per invocation */

	static int inprogress = 0;
	int dbres, result = 0;

	if (!netmodule_due_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "select hostname,destinationip,testspec,extras from moduletests where moduleid=? and location=?", -1, &netmodule_due_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("netmodule_due prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return 0;
		}
	}

	if (!netmodule_purge_sql) {
		dbres = sqlite3_prepare_v2(xymonsqldb, "delete from moduletests where moduleid=? and location=?", -1, &netmodule_purge_sql, NULL);
		if (dbres != SQLITE_OK) {
			errprintf("netmodule_due prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
			return 0;
		}
	}

	if (!inprogress) {
		dbres = sqlite3_bind_text(netmodule_due_sql, 1, module, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(netmodule_due_sql, 2, (location ? location : ""), -1, SQLITE_STATIC);
		if (dbres != SQLITE_OK) return 0;
		inprogress = 1;
	}

	dbres = sqlite3_step(netmodule_due_sql);
	if (dbres == SQLITE_ROW) {
		char *srcip;
		*hostname = sqlite3_column_text(netmodule_due_sql, 0);
		*destination = sqlite3_column_text(netmodule_due_sql, 1);
		*testspec = sqlite3_column_text(netmodule_due_sql, 2);
		*extras = sqlite3_column_text(netmodule_due_sql, 3);
		result = 1;
	}
	else if (dbres == SQLITE_DONE) {
		/* Done - no more tests */
		sqlite3_reset(netmodule_due_sql);

		dbres = sqlite3_bind_text(netmodule_purge_sql, 1, module, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(netmodule_purge_sql, 2, (location ? location : ""), -1, SQLITE_STATIC);
		dbres = sqlite3_step(netmodule_purge_sql);
		sqlite3_reset(netmodule_purge_sql);

		inprogress = 0;
	}

	return result;
}



