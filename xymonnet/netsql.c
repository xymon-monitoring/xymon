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

static sqlite3_stmt *due_query_sql = NULL;
static sqlite3_stmt *due_addrecord_sql = NULL;
static sqlite3_stmt *due_update_sql = NULL;

static sqlite3_stmt *nettest_query_sql = NULL;

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
			dbres = sqlite3_exec(xymonsqldb, "CREATE TABLE testtimes (hostname varchar(200), testspec varchar(400), location varchar(50), destination varchar(200), timestamp int, sourceip varchar(40), interval int, timeout int, testtype int)", NULL, NULL, &err);
			if (dbres != SQLITE_OK) {
				errprintf("Cannot create testtimes table: %s\n", sqlfn, (err ? err : sqlite3_errmsg(xymonsqldb)));
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

	dbres = sqlite3_prepare_v2(xymonsqldb, "update hostip set ip4=?,upd4time=strftime('%s','now') where hostname=?", -1, &dns_ip4update_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("ip4update prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}
	dbres = sqlite3_prepare_v2(xymonsqldb, "update hostip set ip6=?,upd6time=strftime('%s','now') where hostname=?", -1, &dns_ip6update_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("ip6update prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}
	dbres = sqlite3_prepare_v2(xymonsqldb, "insert into hostip(hostname,ip4,ip6,upd4time,upd6time) values (?,?,?,0,0)", -1, &dns_addrecord_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("addrecord prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}
	dbres = sqlite3_prepare_v2(xymonsqldb, "select ip4,upd4time from hostip where hostname=?", -1, &dns_ip4query_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("ip4query prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}
	dbres = sqlite3_prepare_v2(xymonsqldb, "select ip6,upd6time from hostip where hostname=?", -1, &dns_ip6query_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("ip6query prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}

	/*
	 * testtimes table uses the tuple (hostname,testspec,destination) as a unique key.
	 * In most cases that will be (hostname,testspec,'') since 'destination' is empty, 
	 * except for "conn=..." tests that have multiple config-defined destination-IP's.
	 */
	dbres = sqlite3_prepare_v2(xymonsqldb, "select timestamp from testtimes where hostname=? and testspec=? and destination=?", -1, &due_query_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("due_query prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}
	dbres = sqlite3_prepare_v2(xymonsqldb, "insert into testtimes(hostname,testspec,location,destination,testtype,sourceip,timeout,interval,timestamp) values (?,?,?,?,?,?,?,?,strftime('%s','now'))", -1, &due_addrecord_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("due_addrecord prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}
	dbres = sqlite3_prepare_v2(xymonsqldb, "update testtimes set timestamp=strftime('%s','now') where hostname=? and testspec=? and destination=?", -1, &due_update_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("due_update prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}

	dbres = sqlite3_prepare_v2(xymonsqldb, "select hostname,testspec,destination,testtype,sourceip,timeout from testtimes where location=? and (timestamp+interval)<?", -1, &nettest_query_sql, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("nettest_query prep failed: %s\n", sqlite3_errmsg(xymonsqldb));
		return 1;
	}

	dbres = sqlite3_exec(xymonsqldb, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	if (dbres != SQLITE_OK) {
		errprintf("Cannot set async mode: %s\n", sqlite3_errmsg(xymonsqldb));
	}

	return 0;
}

void xymon_sqldb_shutdown(void)
{
	if (dns_ip4update_sql) sqlite3_finalize(dns_ip4update_sql);
	if (dns_ip6update_sql) sqlite3_finalize(dns_ip6update_sql);
	if (dns_addrecord_sql) sqlite3_finalize(dns_addrecord_sql);
	if (dns_ip4query_sql) sqlite3_finalize(dns_ip4query_sql);
	if (dns_ip6query_sql) sqlite3_finalize(dns_ip6query_sql);

	if (due_query_sql) sqlite3_finalize(due_query_sql);
	if (due_addrecord_sql) sqlite3_finalize(due_addrecord_sql);
	if (due_update_sql) sqlite3_finalize(due_update_sql);

	if (nettest_query_sql) sqlite3_finalize(nettest_query_sql);

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


int xymon_sqldb_nettest_due(char *hostname, char *testspec, char *destination, net_test_options_t *options, char *location)
{
	int dbres, result = 1;
	time_t now = getcurrenttime(NULL);

	dbres = sqlite3_bind_text(due_query_sql, 1, hostname, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_query_sql, 2, testspec, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_query_sql, 3, (destination ? destination : ""), -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(due_query_sql);
	if (dbres == SQLITE_ROW) {
		/* Timestamp record exists */
		int timestamp = sqlite3_column_int(due_query_sql, 0);
		sqlite3_reset(due_query_sql);

		if ((timestamp+options->interval) < now) {
			/* Test is due, register the time */
			dbres = sqlite3_bind_text(due_update_sql, 1, hostname, -1, SQLITE_STATIC);
			if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_update_sql, 2, testspec, -1, SQLITE_STATIC);
			if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_update_sql, 3, (destination ? destination : ""), -1, SQLITE_STATIC);
			if (dbres == SQLITE_OK) dbres = sqlite3_step(due_update_sql);
			if (dbres != SQLITE_DONE) errprintf("Error updating due-record %s/%s: %s\n", hostname, testspec, sqlite3_errmsg(xymonsqldb));

			sqlite3_reset(due_update_sql);
		}
		else
			result = 0;
	}
	else {
		/* Create a new record */
		sqlite3_reset(due_query_sql);

		dbres = sqlite3_bind_text(due_addrecord_sql, 1, hostname, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_addrecord_sql, 2, testspec, -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_addrecord_sql, 3, (location ? location : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_addrecord_sql, 4, (destination ? destination : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(due_addrecord_sql, 5, options->testtype);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_addrecord_sql, 6, (options->sourceip ? options->sourceip : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(due_addrecord_sql, 7, options->timeout);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(due_addrecord_sql, 8, options->interval);
		if (dbres == SQLITE_OK) dbres = sqlite3_step(due_addrecord_sql);
		if (dbres != SQLITE_DONE) errprintf("Error adding due-record for %s/%s: %s\n", hostname, testspec, sqlite3_errmsg(xymonsqldb));

		sqlite3_reset(due_addrecord_sql);
	}

	return result;
}

int xymon_sqldb_nettest_row(char *location, char **hostname, char **testspec, char **destination, net_test_options_t *options)
{
	static int inprogress = 0;
	int dbres, result = 0;
	time_t now = getcurrenttime(NULL);

	if (!inprogress) {
		dbres = sqlite3_bind_text(nettest_query_sql, 1, (location ? location : ""), -1, SQLITE_STATIC);
		if (dbres == SQLITE_OK) dbres = sqlite3_bind_int(nettest_query_sql, 2, (int)now);
		if (dbres != SQLITE_OK) return 0;
		inprogress = 1;
	}

	dbres = sqlite3_step(nettest_query_sql);
	if (dbres == SQLITE_ROW) {
		char *srcip;
		*hostname = sqlite3_column_text(nettest_query_sql, 0);
		*testspec = sqlite3_column_text(nettest_query_sql, 1);
		*destination = sqlite3_column_text(nettest_query_sql, 2);
		options->testtype = sqlite3_column_int(nettest_query_sql, 3);
		srcip = sqlite3_column_text(nettest_query_sql, 4);
		options->sourceip = (!srcip || strlen(srcip) == 0) ? NULL : strdup(srcip);
		options->timeout = sqlite3_column_int(nettest_query_sql, 5);
		result = 1;
	}
	else {
		sqlite3_reset(nettest_query_sql);
		inprogress = 0;
	}

	return result;
}

void xymon_sqldb_nettest_rowupdate(char *hostname, char *testspec, char *destination)
{
	int dbres;

	dbres = sqlite3_bind_text(due_update_sql, 1, hostname, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_update_sql, 2, testspec, -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_bind_text(due_update_sql, 3, (destination ? destination : ""), -1, SQLITE_STATIC);
	if (dbres == SQLITE_OK) dbres = sqlite3_step(due_update_sql);
	if (dbres != SQLITE_DONE) errprintf("Error updating due record for %s/%s: %s\n", hostname, testspec, sqlite3_errmsg(xymonsqldb));
	sqlite3_reset(due_update_sql);
}

