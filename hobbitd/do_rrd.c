/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: do_rrd.c,v 1.52 2007-10-23 12:15:01 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>

#include <rrd.h>
#include <pcre.h>

#include "libbbgen.h"

#include "hobbitd_rrd.h"
#include "do_rrd.h"


char *rrddir = NULL;
int  log_double_updates = 1;

static char *exthandler = NULL;
static char **extids = NULL;

static char rrdvalues[MAX_LINE_LEN];

static char *senderip = NULL;
static char rrdfn[PATH_MAX];	/* This one used by the modules */
static char filedir[PATH_MAX];	/* This one used here */

/* How often do we feed data into the RRD file */
#define DEFAULT_RRD_INTERVAL 300
static int  rrdinterval = DEFAULT_RRD_INTERVAL;

#define CACHESZ 6
static int updcache_keyofs = -1;
static RbtHandle updcache;
typedef struct updcacheitem_t {
	char *key;
	char *tpl;
	int valcount;
	char *vals[CACHESZ];
} updcacheitem_t;

static RbtHandle flushtree;
static int have_flushtree = 0;
typedef struct flushtree_t {
	char *hostname;
	time_t flushtime;
} flushtree_t;

void setup_exthandler(char *handlerpath, char *ids)
{
	char *p;
	int idcount = 0;

	MEMDEFINE(rrdvalues);

	exthandler = strdup(handlerpath);
	idcount=1; p = ids; while ((p = strchr(p, ',')) != NULL) { p++; idcount++; }
	extids = (char **)malloc((idcount+1)*(sizeof(char *)));
	idcount = 0;
	p = strtok(ids, ",");
	while (p) {
		extids[idcount++] = strdup(p);
		p = strtok(NULL, ",");
	}
	extids[idcount] = NULL;

	MEMUNDEFINE(rrdvalues);
}

static char *setup_template(char *params[])
{
	int i;
	char *result = NULL;

	for (i = 0; (params[i]); i++) {
		if (strncasecmp(params[i], "DS:", 3) == 0) {
			char *pname, *pend;

			pname = params[i] + 3;
			pend = strchr(pname, ':');
			if (pend) {
				int plen = (pend - pname);

				if (result == NULL) {
					result = (char *)malloc(plen + 1);
					*result = '\0';
				}
				else {
					/* Hackish way of getting the colon delimiter */
					pname--; plen++;
					result = (char *)realloc(result, strlen(result) + plen + 1);
				}
				strncat(result, pname, plen);
			}
		}
	}

	return result;
}

static void setupfn(char *format, char *param)
{
	char *p;

	snprintf(rrdfn, sizeof(rrdfn)-1, format, param);
	rrdfn[sizeof(rrdfn)-1] = '\0';
	while ((p = strchr(rrdfn, ' ')) != NULL) *p = '_';
}

static void setupfn2(char *format, char *param1, char *param2)
{
	char *p;

	snprintf(rrdfn, sizeof(rrdfn)-1, format, param1, param2);
	rrdfn[sizeof(rrdfn)-1] = '\0';
	while ((p = strchr(rrdfn, ' ')) != NULL) *p = '_';
}

static void setupfn3(char *format, char *param1, char *param2, char *param3)
{
	char *p;

	snprintf(rrdfn, sizeof(rrdfn)-1, format, param1, param2, param3);
	rrdfn[sizeof(rrdfn)-1] = '\0';
	while ((p = strchr(rrdfn, ' ')) != NULL) *p = '_';
}

static void setupinterval(int intvl)
{
	rrdinterval = (intvl ? intvl : DEFAULT_RRD_INTERVAL);
}

static int flush_cached_updates(updcacheitem_t *cacheitem, char *newdata)
{
	/* Flush any updates we've cached */
	char *updparams[5+CACHESZ+1] = { "rrdupdate", filedir, "-t", NULL, NULL, NULL, };
	int i, pcount, result;

	dbgprintf("Flushing '%s' with %d updates pending, template '%s'\n", 
		  cacheitem->key, (newdata ? 1 : 0) + cacheitem->valcount, cacheitem->tpl);

	/* ISO C90: parameters cannot be used as initializers */
	updparams[3] = cacheitem->tpl;

	/* Setup the parameter list with all of the cached and new readings */
	for (i=0; (i < cacheitem->valcount); i++) updparams[4+i] = cacheitem->vals[i];
	updparams[4+cacheitem->valcount] = newdata;
	updparams[4+cacheitem->valcount+1] = NULL;

	for (pcount = 0; (updparams[pcount]); pcount++);
	optind = opterr = 0; rrd_clear_error();
	result = rrd_update(pcount, updparams);

	/* Clear the cached data */
	for (i=0; (i < cacheitem->valcount); i++) xfree(cacheitem->vals[i]);
	cacheitem->valcount = 0;

	return result;
}

static int create_and_update_rrd(char *hostname, char *testname, char *creparams[], char *template)
{
	static int callcounter = 0;
	struct stat st;
	int pcount, result;
	char *updcachekey;
	RbtIterator handle;
	updcacheitem_t *cacheitem = NULL;
	int pollinterval;

	/* Reset the RRD poll interval */
	pollinterval = rrdinterval;
	rrdinterval = DEFAULT_RRD_INTERVAL;

	if ((rrdfn == NULL) || (strlen(rrdfn) == 0)) {
		errprintf("RRD update for no file\n");
		return -1;
	}

	MEMDEFINE(rrdvalues);
	MEMDEFINE(filedir);

	sprintf(filedir, "%s/%s", rrddir, hostname);
	if (stat(filedir, &st) == -1) {
		if (mkdir(filedir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) == -1) {
			errprintf("Cannot create rrd directory %s : %s\n", filedir, strerror(errno));
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return -1;
		}
	}
	/* Watch out here - "rrdfn" may be very large. */
	snprintf(filedir, sizeof(filedir)-1, "%s/%s/%s", rrddir, hostname, rrdfn);
	filedir[sizeof(filedir)-1] = '\0'; /* Make sure it is null terminated */

	/* Prepare to cache the update. Create the cache tree, and find/create a cache record */
	if (updcache_keyofs == -1) {
		updcache = rbtNew(name_compare);
		updcache_keyofs = strlen(rrddir);
	}
	updcachekey = filedir + updcache_keyofs;
	handle = rbtFind(updcache, updcachekey);
	if (handle == rbtEnd(updcache)) {
		cacheitem = (updcacheitem_t *)calloc(1, sizeof(updcacheitem_t));
		cacheitem->key = strdup(updcachekey);
		cacheitem->tpl = (template ? strdup(template) : setup_template(creparams));
		rbtInsert(updcache, cacheitem->key, cacheitem);
	}
	else {
		cacheitem = (updcacheitem_t *)gettreeitem(updcache, handle);
	}

	/* If the RRD file doesn't exist, create it immediately */
	if (stat(filedir, &st) == -1) {
		char **rrdcreate_params, **rrddefinitions;
		int rrddefcount, i;
		char *rrakey = NULL;
		char stepsetting[10];

		dbgprintf("Creating rrd %s\n", filedir);

		/* How many parameters did we get? */
		for (pcount = 0; (creparams[pcount]); pcount++);

		/* Add the RRA definitions to the create parameter set */
		if (pollinterval != DEFAULT_RRD_INTERVAL) {
			rrakey = (char *)malloc(strlen(testname) + 10);
			sprintf(rrakey, "%s/%d", testname, pollinterval);
		}
		sprintf(stepsetting, "%d", pollinterval);

		rrddefinitions = get_rrd_definition((rrakey ? rrakey : testname), &rrddefcount);
		rrdcreate_params = (char **)calloc(4 + pcount + rrddefcount + 1, sizeof(char *));
		rrdcreate_params[0] = "rrdcreate";
		rrdcreate_params[1] = filedir;
		rrdcreate_params[2] = "-s";
		rrdcreate_params[3] = stepsetting;
		for (i=0; (i < pcount); i++)
			rrdcreate_params[4+i]      = creparams[i];
		for (i=0; (i < rrddefcount); i++, pcount++)
			rrdcreate_params[4+pcount] = rrddefinitions[i];

		if (debug) {
			for (i = 0; (rrdcreate_params[i]); i++) {
				dbgprintf("RRD create param %02d: '%s'\n", i, rrdcreate_params[i]);
			}
		}

		/*
		 * Ugly! RRDtool uses getopt() for parameter parsing, so
		 * we MUST reset this before every call.
		 */
		optind = opterr = 0; rrd_clear_error();
		result = rrd_create(4+pcount, rrdcreate_params);
		xfree(rrdcreate_params);
		if (rrakey) xfree(rrakey);

		if (result != 0) {
			errprintf("RRD error creating %s: %s\n", filedir, rrd_get_error());
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return 1;
		}
	}

	/* 
	 * To smooth the load, we force the update through for every CACHESZ updates.
	 * This gives us a steady load during the initial cache fill period.
	 */
	if (++callcounter < CACHESZ) {
		if (cacheitem && (cacheitem->valcount < CACHESZ)) {
			/* 
			 * There's room for caching this update.
			 */ 
			cacheitem->vals[cacheitem->valcount] = strdup(rrdvalues);
			cacheitem->valcount += 1;
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return 0;
		}
	}
	else callcounter = 0;

	/* At this point, we will commit the update to disk */
	result = flush_cached_updates(cacheitem, rrdvalues);
	if (result != 0) {
		char *msg = rrd_get_error();

		if (log_double_updates || (strncmp(msg, "illegal attempt", 15) != 0)) {
			errprintf("RRD error updating %s from %s: %s\n", 
				  filedir, (senderip ? senderip : "unknown"), msg);
		}

		MEMUNDEFINE(filedir);
		MEMUNDEFINE(rrdvalues);
		return 2;
	}

	MEMUNDEFINE(filedir);
	MEMUNDEFINE(rrdvalues);

	return 0;
}

void rrdcacheflushall(void)
{
	RbtIterator handle;
	updcacheitem_t *cacheitem;

	if (updcache_keyofs == -1) return; /* No cache */

	for (handle = rbtBegin(updcache); (handle != rbtEnd(updcache)); handle = rbtNext(updcache, handle)) {
		cacheitem = (updcacheitem_t *) gettreeitem(updcache, handle);
		if (cacheitem->valcount > 0) {
			sprintf(filedir, "%s%s", rrddir, cacheitem->key);
			flush_cached_updates(cacheitem, NULL);
		}
	}
}

void rrdcacheflushhost(char *hostname)
{
	RbtIterator handle;
	updcacheitem_t *cacheitem;
	flushtree_t *flushitem;
	int keylen, done;
	time_t now = getcurrenttime(NULL);

	if (updcache_keyofs == -1) return;

	/* If we get a full path for the key, skip the leading rrddir */
	if (strncmp(hostname, rrddir, updcache_keyofs) == 0) hostname += updcache_keyofs;
	keylen = strlen(hostname);

	if (!have_flushtree) {
		flushtree = rbtNew(name_compare);
		have_flushtree = 1;
	}
	handle = rbtFind(flushtree, hostname);
	if (handle == rbtEnd(flushtree)) {
		flushitem = (flushtree_t *)calloc(1, sizeof(flushtree_t));
		flushitem->hostname = strdup(hostname);
		flushitem->flushtime = 0;
		rbtInsert(flushtree, flushitem->hostname, flushitem);
	}
	else {
		flushitem = (flushtree_t *) gettreeitem(flushtree, handle);
	}

	if ((flushitem->flushtime + 60) >= now) {
		dbgprintf("Flush of '%s' skipped, too soon\n", hostname);
		return;
	}
	flushitem->flushtime = now;

	handle = rbtBegin(updcache); 
	while (handle != rbtEnd(updcache)) {
		cacheitem = (updcacheitem_t *) gettreeitem(updcache, handle);

		switch (strncasecmp(cacheitem->key, hostname, keylen)) {
		  case 1 :
			handle = rbtEnd(updcache); break;

		  case 0:
			if (cacheitem->valcount > 0) {
				dbgprintf("Flushing cache '%s'\n", cacheitem->key);
				sprintf(filedir, "%s%s", rrddir, cacheitem->key);
				flush_cached_updates(cacheitem, NULL);
			}
			/* Fall through */

		  default:
			handle = rbtNext(updcache, handle);
			break;
		}
	}
}

static int rrddatasets(char *hostname, char ***dsnames)
{
	struct stat st;

	int result;
	char *fetch_params[] = { "rrdfetch", filedir, "AVERAGE", "-s", "-30m", NULL };
	time_t starttime, endtime;
	unsigned long steptime, dscount;
	rrd_value_t *rrddata;

	snprintf(filedir, sizeof(filedir)-1, "%s/%s/%s", rrddir, hostname, rrdfn);
	filedir[sizeof(filedir)-1] = '\0';
	if (stat(filedir, &st) == -1) return 0;

	optind = opterr = 0; rrd_clear_error();
	result = rrd_fetch(5, fetch_params, &starttime, &endtime, &steptime, &dscount, dsnames, &rrddata);
	if (result == -1) {
		errprintf("Error while retrieving RRD dataset names from %s: %s\n",
			  filedir, rrd_get_error());
		return 0;
	}

	free(rrddata);	/* No use for the actual data */
	return dscount;
}


/* Include all of the sub-modules. */
#include "rrd/do_bbgen.c"
#include "rrd/do_bbtest.c"
#include "rrd/do_bbproxy.c"
#include "rrd/do_hobbitd.c"
#include "rrd/do_citrix.c"
#include "rrd/do_ntpstat.c"

#include "rrd/do_memory.c"	/* Must go before do_la.c */
#include "rrd/do_la.c"
#include "rrd/do_disk.c"
#include "rrd/do_netstat.c"
#include "rrd/do_vmstat.c"
#include "rrd/do_iostat.c"
#include "rrd/do_ifstat.c"

#include "rrd/do_apache.c"
#include "rrd/do_bind.c"
#include "rrd/do_sendmail.c"
#include "rrd/do_mailq.c"
#include "rrd/do_iishealth.c"
#include "rrd/do_temperature.c"

#include "rrd/do_net.c"

#include "rrd/do_ncv.c"
#include "rrd/do_external.c"
#include "rrd/do_filesizes.c"
#include "rrd/do_counts.c"
#include "rrd/do_trends.c"
#include "rrd/do_paging.c"

#include "rrd/do_ifmib.c"

void update_rrd(char *hostname, char *testname, char *msg, time_t tstamp, char *sender, hobbitrrd_t *ldef)
{
	int res = 0;
	char *id;

	MEMDEFINE(rrdvalues);

	if (ldef) id = ldef->hobbitrrdname; else id = testname;
	senderip = sender;

	if      (strcmp(id, "bbgen") == 0)       res = do_bbgen_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bbtest") == 0)      res = do_bbtest_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bbproxy") == 0)     res = do_bbproxy_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "hobbitd") == 0)     res = do_hobbitd_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "citrix") == 0)      res = do_citrix_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "ntpstat") == 0)     res = do_ntpstat_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "la") == 0)          res = do_la_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "disk") == 0)        res = do_disk_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "memory") == 0)      res = do_memory_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "netstat") == 0)     res = do_netstat_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "vmstat") == 0)      res = do_vmstat_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "iostat") == 0)      res = do_iostat_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "ifstat") == 0)      res = do_ifstat_rrd(hostname, testname, msg, tstamp);

	/* These two come from the filerstats2bb.pl script. The reports are in disk-format */
	else if (strcmp(id, "inode") == 0)       res = do_disk_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "qtree") == 0)       res = do_disk_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "apache") == 0)      res = do_apache_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bind") == 0)        res = do_bind_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "sendmail") == 0)    res = do_sendmail_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "mailq") == 0)       res = do_mailq_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "iishealth") == 0)   res = do_iishealth_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "temperature") == 0) res = do_temperature_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "ncv") == 0)         res = do_ncv_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "tcp") == 0)         res = do_net_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "filesizes") == 0)   res = do_filesizes_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "proccounts") == 0)  res = do_counts_rrd("processes", hostname, testname, msg, tstamp);
	else if (strcmp(id, "portcounts") == 0)  res = do_counts_rrd("ports", hostname, testname, msg, tstamp);
	else if (strcmp(id, "linecounts") == 0)  res = do_derives_rrd("lines", hostname, testname, msg, tstamp);
	else if (strcmp(id, "paging") == 0)      res = do_paging_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "trends") == 0)      res = do_trends_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "ifmib") == 0)       res = do_ifmib_rrd(hostname, testname, msg, tstamp);

	else if (extids && exthandler) {
		int i;

		for (i=0; (extids[i] && strcmp(extids[i], id)); i++) ;

		if (extids[i]) res = do_external_rrd(hostname, testname, msg, tstamp);
	}

	senderip = NULL;

	MEMUNDEFINE(rrdvalues);
}

