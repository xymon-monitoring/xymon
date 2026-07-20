/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <utime.h>

#include <rrd.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "libxymon.h"
#include "../lib/rrd_api_compat.h"

#include "xymond_rrd.h"
#include "do_rrd.h"
#include "client_config.h"

#ifndef NAME_MAX
#define NAME_MAX 255	/* Solaris doesn't define NAME_MAX, but ufs limit is 255 */
#endif

extern int seq;	/* from xymond_rrd.c */

char *rrddir = NULL;
int use_rrd_cache = 1;         /* Use the cache by default */
int no_rrd = 0;                /* Write to rrd by default */

static int  processorfd = 0;
static FILE *processorstream = NULL;

static char *exthandler = NULL;
static char **extids = NULL;

static char rrdvalues[MAX_LINE_LEN];

static char *senderip = NULL;
static char rrdfn[PATH_MAX];   /* Base filename without directories, from setupfn() */
static char filedir[PATH_MAX]; /* Full path filename */
static char *fnparams[4] = { NULL, };  /* Saved parameters passed to setupfn() */

/* How often do we feed data into the RRD file */
#define DEFAULT_RRD_INTERVAL 300
static int  rrdinterval = DEFAULT_RRD_INTERVAL;

#define CACHESZ 12             /* # of updates that can be cached - updates are usually 5 minutes apart */
static int updcache_keyofs = -1;
static void * updcache;
typedef struct updcacheitem_t {
	char *key;
	rrdtpldata_t *tpl;
	int tplowned;		/* tpl was built by setup_template() for this entry
				 * (handler passed none) - evict/drop must free it.
				 * Built-in handlers pass a shared static template. */
	int valcount;
	char *vals[CACHESZ];
	int updseq[CACHESZ];
	time_t updtime[CACHESZ];
	time_t lasttouch;	/* for idle eviction under instance churn */
} updcacheitem_t;

/* Free a cache entry's owned template: the per-entry rrdtpldata_t that
 * setup_template() built (template string, dsnames tree and its records).
 * Shared static templates (tplowned == 0) are left alone. */
static void free_owned_tpl(updcacheitem_t *cacheitem)
{
	rrdtpldata_t *tpl = cacheitem->tpl;
	xtreePos_t handle;

	if (!cacheitem->tplowned || !tpl) return;
	for (handle = xtreeFirst(tpl->dsnames); (handle != xtreeEnd(tpl->dsnames)); handle = xtreeNext(tpl->dsnames, handle)) {
		rrdtplnames_t *nam = (rrdtplnames_t *)xtreeData(tpl->dsnames, handle);
		if (nam->dsnam) xfree(nam->dsnam);
		xfree(nam);
	}
	xtreeDestroy(tpl->dsnames);
	if (tpl->template) xfree(tpl->template);
	xfree(tpl);
	cacheitem->tpl = NULL;
}

static void * flushtree;
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

void setup_extprocessor(char *cmd)
{

	int n;
	int pfd[2];
	pid_t childpid;

	if (!cmd) return;

	processorfd = 0;

	n = pipe(pfd);
	if (n == -1) {
		errprintf("Could not get a pipe: %s\n", strerror(errno));
	}
	else {
		childpid = fork();
		if (childpid == -1) {
			errprintf("Could not fork channel handler: %s\n", strerror(errno));
		}
		else if (childpid == 0) {
			/* The channel handler child */
			char *argv[2];

			argv[0] = strdup(cmd);
			argv[1] = NULL;

			n = dup2(pfd[0], STDIN_FILENO);
			close(pfd[0]); close(pfd[1]);
			n = execvp(cmd, argv);

			/* We should never go here */
			errprintf("exec() failed for child command %s: %s\n", cmd, strerror(errno));
			exit(1);
		}
		else {
			/* Parent process continues */
			close(pfd[0]);
			processorfd = pfd[1];
			processorstream = fdopen(processorfd, "w");
			errprintf("External processor '%s' started\n", cmd);
		}
	}
}

void shutdown_extprocessor(void)
{
	if (!processorfd) return;

	close(processorfd);
	processorfd = 0;
	processorstream = NULL;

	errprintf("External processor stopped\n");
}


static void setupfn(char *format, char *param)
{
	char *p;

	memset(fnparams, 0, sizeof(fnparams));
	fnparams[0] = param;

	snprintf(rrdfn, sizeof(rrdfn)-1, format, param);
	rrdfn[sizeof(rrdfn)-1] = '\0';
	while ((p = strchr(rrdfn, ' ')) != NULL) *p = '_';
}

static void setupfn2(char *format, char *param1, char *param2)
{
	char *p;

	while ((p = strchr(param2, '/')) != NULL) *p = ',';

	memset(fnparams, 0, sizeof(fnparams));
	fnparams[0] = param1;
	fnparams[1] = param2;

	snprintf(rrdfn, sizeof(rrdfn)-1, format, param1, param2);
	rrdfn[sizeof(rrdfn)-1] = '\0';
	while ((p = strchr(rrdfn, ' ')) != NULL) *p = '_';

	if (strlen(rrdfn) >= (NAME_MAX - 50)) {
		/*
		 * Filename is too long for one directory entry (instance names
		 * reach here from arbitrary status content, and the reversible
		 * encoding can triple their length). Same md5 fallback as
		 * setupfn3(): a bounded, stable, collision-free name.
		 */
		char *hash = md5hash(rrdfn+(NAME_MAX-50));

		sprintf(rrdfn+(NAME_MAX-50), "_%s.rrd", hash);
	}
}

/* Finish a caller-reconstructed LEGACY basename the way setupfn2() did
 * when the file was originally created: space mangling, nothing more. The
 * legacy writer never shortened names - anything up to NAME_MAX landed on
 * disk verbatim - so the migration candidate must be the raw name;
 * applying today's md5 shortening here would miss every legacy file in
 * the [NAME_MAX-50, NAME_MAX) range and silently restart its history. */
static void legacyfn_finish(char *fn)
{
	char *p;

	while ((p = strchr(fn, ' ')) != NULL) *p = '_';
}

static void setupfn3(char *format, char *param1, char *param2, char *param3)
{
	char *p;

	memset(fnparams, 0, sizeof(fnparams));
	fnparams[0] = param1;
	fnparams[1] = param2;
	fnparams[2] = param3;

	snprintf(rrdfn, sizeof(rrdfn)-1, format, param1, param2, param3);
	rrdfn[sizeof(rrdfn)-1] = '\0';
	while ((p = strchr(rrdfn, ' ')) != NULL) *p = '_';

	if (strlen(rrdfn) >= (NAME_MAX - 50)) {
		/*
		 * Filename is too long. Limit filename length
		 * by replacing the last part of the filename
		 * with an MD5 hash.
		 */
		char *hash = md5hash(rrdfn+(NAME_MAX-50));

		sprintf(rrdfn+(NAME_MAX-50), "_%s.rrd", hash);
	}
}

static void setupinterval(int intvl)
{
	rrdinterval = (intvl ? intvl : DEFAULT_RRD_INTERVAL);
}

static int flush_cached_updates(updcacheitem_t *cacheitem, char *newdata)
{
	/* Flush any updates we've cached */
	xymon_rrd_argv_item_t updparams[5+CACHESZ+1] = { "rrdupdate", filedir, "-t", NULL, NULL, NULL, };
	int i, pcount, result;
	time_t maxts = 0;

	/* The newest data timestamp in this batch - committed to the fileset
	 * index below IF rrdtool accepts the batch. */
	for (i=0; (i < cacheitem->valcount); i++)
		if (cacheitem->updtime[i] > maxts) maxts = cacheitem->updtime[i];
	if (newdata) {
		time_t newts = (time_t)atol(newdata);
		if (newts > maxts) maxts = newts;
	}

	dbgprintf("Flushing '%s' with %d updates pending, template '%s'\n", 
		  cacheitem->key, (newdata ? 1 : 0) + cacheitem->valcount, cacheitem->tpl->template);

	/* ISO C90: parameters cannot be used as initializers */
	updparams[3] = cacheitem->tpl->template;

	/* Setup the parameter list with all of the cached and new readings */
	for (i=0; (i < cacheitem->valcount); i++) updparams[4+i] = cacheitem->vals[i];

	if (newdata) {
		updparams[4+cacheitem->valcount] = newdata;
		updparams[4+cacheitem->valcount+1] = NULL;
	}
	else {
		/* No new data - happens when flushing the cache */
		updparams[4+cacheitem->valcount] = NULL;
	}

	for (pcount = 0; (updparams[pcount]); pcount++);
	optind = opterr = 0; rrd_clear_error();
	result = xymon_rrd_update(pcount, updparams);

#if defined(LINUX)
	/*
	 * RRDtool 1.2+ uses mmap'ed I/O, but the Linux kernel does not update timestamps when
	 * doing file I/O on mmap'ed files. This breaks our check for stale/nostale RRD's.
	 * So do an explicit timestamp update on the file here - but only for an ACCEPTED
	 * update: the mtime is the file's durable freshness (stale filters, fileset-index
	 * readers), and touching it for a rejected batch would make a chronically broken
	 * producer look fresh forever.
	 */
	if (result == 0) utimes(filedir, NULL);
#endif

	/* Clear the cached data */
	for (i=0; (i < cacheitem->valcount); i++) {
		cacheitem->updseq[i] = 0;
		cacheitem->updtime[i] = 0;
		if (cacheitem->vals[i]) xfree(cacheitem->vals[i]);
	}
	cacheitem->valcount = 0;

	/* Accepted data advances the fileset index's freshness; a rejected
	 * batch (dropped above either way) leaves it alone. The cache key is
	 * "/<host>/<rrdfn>" - split it back apart for the note. */
	if ((result == 0) && maxts) {
		char *keyhost = cacheitem->key + 1;
		char *slash = strchr(keyhost, '/');

		if (slash) {
			char savedhost[PATH_MAX];
			size_t hlen = slash - keyhost;

			if (hlen < sizeof(savedhost)) {
				memcpy(savedhost, keyhost, hlen); savedhost[hlen] = '\0';
				fsidx_note_commit(rrddir, savedhost, slash+1, maxts);
			}
		}
	}

	return result;
}

static int create_and_update_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *creparams[], void *template)
{
	static int callcounter = 0;
	struct stat st;
	int pcount, result;
	char *updcachekey;
	xtreePos_t handle;
	updcacheitem_t *cacheitem = NULL;
	int pollinterval;
	strbuffer_t *modifymsg;
	time_t updtime = 0;

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
	/* Watch out here - "rrdfn" may be very large. A truncated path
	 * could hit the wrong file; refuse instead. */
	if ((size_t)snprintf(filedir, sizeof(filedir), "%s/%s/%s", rrddir, hostname, rrdfn) >= sizeof(filedir)) {
		errprintf("RRD path for %s/%s exceeds PATH_MAX - update dropped\n", hostname, rrdfn);
		MEMUNDEFINE(filedir);
		MEMUNDEFINE(rrdvalues);
		return -1;
	}

	/* The graph definition's storage filters: EXSTOREPATTERN drops the
	 * instance entirely, STOREPATTERN keeps only matching instances. */
	if (!xymon_gdef_store_allowed(rrdfn)) {
		dbgprintf("Store filter drops %s\n", rrdfn);
		MEMUNDEFINE(filedir); MEMUNDEFINE(rrdvalues);
		return 0;
	}

	/* 
	 * Prepare to cache the update. Create the cache tree, and find/create a cache record.
	 * Note: Cache records are persistent, once created they remain in place forever.
	 * Only the update-data is flushed from time to time.
	 */
	if (updcache_keyofs == -1) {
		updcache = xtreeNew(strcasecmp);
		updcache_keyofs = strlen(rrddir);
	}
	updcachekey = filedir + updcache_keyofs;
	handle = xtreeFind(updcache, updcachekey);
	if (handle == xtreeEnd(updcache)) {
		int tplowned = 0;

		if (!template) { template = setup_template(creparams); tplowned = 1; }
		if (!template) {
			errprintf("BUG: setup_template() returns NULL! host=%s,test=%s,cp[0]=%s, cp[1]=%s\n",
				  hostname, testname,
				  (creparams[0] ? creparams[0] : "NULL"),
				  (creparams[1] ? creparams[1] : "NULL"));
			return -1;
		}
		cacheitem = (updcacheitem_t *)calloc(1, sizeof(updcacheitem_t));
		cacheitem->key = strdup(updcachekey);
		cacheitem->tpl = template;
		cacheitem->tplowned = tplowned;
		xtreeAdd(updcache, cacheitem->key, cacheitem);
	}
	else {
		cacheitem = (updcacheitem_t *)xtreeData(updcache, handle);
		if (!template) template = cacheitem->tpl;
	}
	cacheitem->lasttouch = gettimer();

	/* If the RRD file doesn't exist, create it immediately */
	if (stat(filedir, &st) == -1) {
		xymon_rrd_argv_item_t *rrdcreate_params;
		char **rrddefinitions;
		char *cfextras[16];
		int cfextracount = 0;
		int rrddefcount, i;
		char *rrakey = NULL;
		char stepsetting[10];
		int havestepsetting = 0, fixcount = 2;

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

		/* Derived consolidations: gdefs matching this file may read
		 * MIN/MAX/LAST DEFs - clone each AVERAGE archive per extra CF
		 * the gdefs declare, unless the definition set already carries
		 * that CF. A file no gdef reads beyond AVERAGE gets exactly
		 * the stock archive set (safe by default). Late gdef changes
		 * are the parked schema-evolution tune-pass, forward-only. */
		{
			int cfs = xymon_gdef_cfs_forfile(rrdfn) & ~XYMON_CF_AVERAGE;
			struct { int bit; char *name; } cfnames[] = {
				{ XYMON_CF_MIN, "MIN" }, { XYMON_CF_MAX, "MAX" }, { XYMON_CF_LAST, "LAST" }, { 0, NULL }
			};
			int c;

			cfextracount = 0;
			for (c = 0; (cfs && cfnames[c].name); c++) {
				int have = 0;
				char prefix[32];

				if (!(cfs & cfnames[c].bit)) continue;
				snprintf(prefix, sizeof(prefix), "RRA:%s:", cfnames[c].name);
				for (i = 0; (!have && (i < rrddefcount)); i++)
					have = (strncmp(rrddefinitions[i], prefix, strlen(prefix)) == 0);
				if (have) continue;
				for (i = 0; (i < rrddefcount); i++) {
					if (strncmp(rrddefinitions[i], "RRA:AVERAGE:", 12) != 0) continue;
					if (cfextracount >= (int)(sizeof(cfextras)/sizeof(cfextras[0]))) {
						errprintf("%s: derived-RRA cap (%d) reached, some %s archives skipped - rrdreconcile --apply can add them later\n",
							  rrdfn, cfextracount, cfnames[c].name);
						break;
					}
					{
						size_t xlen = strlen(cfnames[c].name) + strlen(rrddefinitions[i]) + 8;
						cfextras[cfextracount] = (char *)malloc(xlen);
						snprintf(cfextras[cfextracount], xlen, "RRA:%s:%s", cfnames[c].name, rrddefinitions[i] + 12);
						cfextracount++;
					}
				}
			}
		}

		rrdcreate_params = calloc(6 + pcount + rrddefcount + cfextracount + 1, sizeof(*rrdcreate_params));
		rrdcreate_params[0] = "rrdcreate";
		rrdcreate_params[1] = filedir;

		/* Is there already a step-setting in the rrddefinitions? */
		for (i=0; (!havestepsetting && (i < rrddefcount)); i++) 
			havestepsetting = ((strcmp(rrddefinitions[i], "-s") == 0) || (strcmp(rrddefinitions[i], "--step") == 0));
		if (!havestepsetting) {
			rrdcreate_params[2] = "-s";
			rrdcreate_params[3] = stepsetting;
			fixcount = 4;
		}

		for (i=0; (i < pcount); i++)
			rrdcreate_params[fixcount+i]      = creparams[i];
		for (i=0; (i < rrddefcount); i++, pcount++)
			rrdcreate_params[fixcount+pcount] = rrddefinitions[i];
		for (i=0; (i < cfextracount); i++, pcount++)
			rrdcreate_params[fixcount+pcount] = cfextras[i];

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
		result = xymon_rrd_create(fixcount+pcount, rrdcreate_params);
		xfree(rrdcreate_params);
		for (i=0; (i < cfextracount); i++) xfree(cfextras[i]);
		if (rrakey) xfree(rrakey);

		if (result != 0) {
			errprintf("RRD error creating %s: %s\n", filedir, rrd_get_error());
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return 1;
		}
	}

	updtime = atoi(rrdvalues);
	if (cacheitem->valcount > 0) {
		/* Check for duplicate updates */

		if (cacheitem->updseq[cacheitem->valcount-1] == seq) {
			/*
			 * This is usually caused by a configuration error, 
			 * e.g. two PORT settings in analysis.cfg that
			 * use the same TRACK string.
			 * Can also be two web checks using the same URL, but
			 * with different POST data.
			 */
			dbgprintf("%s/%s: Error - ignored duplicate update for message sequence %d\n", hostname, rrdfn, seq);
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return 0;
		}
		else if (cacheitem->updtime[cacheitem->valcount-1] > updtime) {
			dbgprintf("%s/%s: Error - RRD time goes backwards: Now=%d, previous=%d\n", hostname, rrdfn, (int) updtime, (int)cacheitem->updtime[cacheitem->valcount-1]);
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return 0;
		}
		else if (cacheitem->updtime[cacheitem->valcount-1] == updtime) {
			int identical = (strcmp(rrdvalues, cacheitem->vals[cacheitem->valcount-1]) == 0);

			if (!identical) {
				int i;

				errprintf("%s/%s: Bug - duplicate RRD data with same timestamp %d, different data\n", 
					  hostname, rrdfn, (int) updtime);

				for (i=0; (i < cacheitem->valcount); i++) 
					dbgprintf("Val %d: Seq %d: %s\n", i, cacheitem->updseq[i], cacheitem->vals[i]);
				dbgprintf("NewVal: Seq %d: %s\n", seq, rrdvalues);
			}
			else {
				dbgprintf("%s/%s: Ignored duplicate (and identical) update timestamped %d\n", hostname, rrdfn, (int) updtime);
			}

			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return 0;
		}
	}


	/*
	 * Match the RRD data against any DS client-configuration modifiers,
	 * and feed the current-values store used by AGGDS aggregate rules
	 * (evaluated at message-batch end, not per file).
	 */
	modifymsg = check_rrdds_thresholds(hostname, classname, pagepaths, rrdfn, ((rrdtpldata_t *)template)->dsnames, rrdvalues);
	if (modifymsg) combo_add(modifymsg);
	update_aggds_store(hostname, rrdfn, ((rrdtpldata_t *)template)->dsnames, rrdvalues);

	/*
	 * See if we want the data to go to an external handler.
	 */
	if (processorstream) {
		int i, n;

		n = fprintf(processorstream, "%s %s %s", ((rrdtpldata_t *)template)->template, rrdvalues, hostname);
		for (i=0; ((n >= 0) && fnparams[i]); i++) n = fprintf(processorstream, " %s", fnparams[i]);
		if (n >= 0) n = fprintf(processorstream, "\n");
		if (n >= 0) fflush(processorstream);

		if (n == -1) {
			errprintf("Ext-processor write failed: %s\n", strerror(errno));
			shutdown_extprocessor();
		}
	}

	/* Are we actually handling the writing of RRD files? */
	if (no_rrd) return 0;

	/* Bookkeep the fileset index: the entry and its declared schema are
	 * noted now; the freshness timestamp advances only when the update
	 * COMMITS (flush_cached_updates), so a chronically rejected update
	 * cannot keep an instance looking fresh forever. */
	fsidx_note_schema(rrddir, hostname, rrdfn, (time_t)updtime);

	/* 
	 * We cannot just cache data every time because then after CACHESZ updates
	 * of each RRD, we will flush all of the data at once (all of the caches 
	 * fill at the same speed); this would result in huge load-spikes every 
	 * rrdinterval*CACHESZ seconds.
	 *
	 * So to smooth the load, we force the update through for every CACHESZ 
	 * updates, regardless of how much is in the cache. This gives us a steady 
	 * (although slightly higher) load.
	 */
	if (use_rrd_cache && (++callcounter < CACHESZ)) {
		if (cacheitem && (cacheitem->valcount < CACHESZ)) {
			cacheitem->updseq[cacheitem->valcount] = seq;
			cacheitem->updtime[cacheitem->valcount] = updtime;
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

		if (strstr(msg, "(minimum one second step)") != NULL) {
			dbgprintf("RRD error updating %s from %s: %s\n", 
				  filedir, (senderip ? senderip : "unknown"), msg);
		}
		else {
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

/* Evict update-cache entries idle beyond maxage. The cache is pure
 * batching: flushing an idle entry's pending values and dropping it
 * loses nothing - this
 * bounds the memory that instance churn (container mounts, rotating
 * names) used to grow forever. Keys are collected first: deleting
 * while traversing the tree is not safe. A per-entry template (built by
 * setup_template for handlers that pass none) is freed with the entry;
 * shared static templates are not. */
void rrdcache_evict_idle(time_t maxage)
{
	xtreePos_t handle;
	time_t now = gettimer();
	char **keys = NULL;
	int nkeys = 0, i;
	char **hosts = NULL;
	int nhosts = 0;

	if (updcache_keyofs == -1) return;

	for (handle = xtreeFirst(updcache); (handle != xtreeEnd(updcache)); handle = xtreeNext(updcache, handle)) {
		updcacheitem_t *cacheitem = (updcacheitem_t *)xtreeData(updcache, handle);
		if ((now - cacheitem->lasttouch) <= maxage) continue;
		keys = (char **)realloc(keys, (nkeys+1) * sizeof(char *));
		keys[nkeys++] = cacheitem->key;
	}

	for (i = 0; (i < nkeys); i++) {
		handle = xtreeFind(updcache, keys[i]);
		if (handle != xtreeEnd(updcache)) {
			updcacheitem_t *cacheitem = (updcacheitem_t *)xtreeData(updcache, handle);
			int v;

			if (cacheitem->valcount > 0) {
				sprintf(filedir, "%s%s", rrddir, cacheitem->key);
				flush_cached_updates(cacheitem, NULL);

				/* That flush noted a freshness commit in the in-memory
				 * index only. An idle host gets no later message-driven
				 * fsidx_flush(), so remember the host ("/<host>/<rrdfn>"
				 * key) and push its index to disk below. */
				{
					char *keyhost = cacheitem->key + 1;
					char *slash = strchr(keyhost, '/');

					if (slash) {
						size_t hlen = slash - keyhost;

						for (v = 0; (v < nhosts); v++)
							if ((strncasecmp(hosts[v], keyhost, hlen) == 0) && (hosts[v][hlen] == '\0')) break;
						if (v == nhosts) {
							hosts = (char **)realloc(hosts, (nhosts+1) * sizeof(char *));
							hosts[nhosts] = (char *)malloc(hlen + 1);
							memcpy(hosts[nhosts], keyhost, hlen); hosts[nhosts][hlen] = '\0';
							nhosts++;
						}
					}
				}
			}
			xtreeDelete(updcache, cacheitem->key);
			for (v = 0; (v < cacheitem->valcount); v++)
				if (cacheitem->vals[v]) xfree(cacheitem->vals[v]);
			free_owned_tpl(cacheitem);
			xfree(cacheitem->key);
			xfree(cacheitem);
		}
	}
	if (nkeys) dbgprintf("updcache: evicted %d idle entries\n", nkeys);
	if (keys) xfree(keys);

	for (i = 0; (i < nhosts); i++) {
		fsidx_flush_now(rrddir, hosts[i]);
		xfree(hosts[i]);
	}
	if (hosts) xfree(hosts);

	if (nkeys) {
		/* The fallback array-backed xtree reclaims a deleted slot only
		 * when the same key is re-added, and churned instance names never
		 * return - each eviction would leave a tombstone (slot + private
		 * key copy) forever, defeating the point of evicting. Rebuild the
		 * tree with the survivors; xtreeDestroy frees only tombstone
		 * copies, never live keys, so moving the entries is safe on both
		 * xtree variants. */
		void *rebuilt = xtreeNew(strcasecmp);

		for (handle = xtreeFirst(updcache); (handle != xtreeEnd(updcache)); handle = xtreeNext(updcache, handle)) {
			updcacheitem_t *cacheitem = (updcacheitem_t *)xtreeData(updcache, handle);
			xtreeAdd(rebuilt, cacheitem->key, cacheitem);
		}
		xtreeDestroy(updcache);
		updcache = rebuilt;
	}
}

/* Remove one host's cache entries. Flushed first on rename - the files
 * are about to move, the pending data must land in them before they do.
 * Discarded on drophost - a flush would write into the directory the
 * forked deletion is tearing down, and the values are the dropped
 * host's data anyway. (rrdcacheflushhost() is not usable here: it
 * expects "/host"-shaped keys and rate-limits to one flush per 60s.) */
void rrdcache_drop_host(char *hostname, int flushfirst)
{
	xtreePos_t handle;
	char prefix[PATH_MAX];
	size_t plen;
	char **keys = NULL;
	int nkeys = 0, i;

	if ((updcache_keyofs == -1) || !hostname) return;
	snprintf(prefix, sizeof(prefix), "/%s/", hostname);
	plen = strlen(prefix);

	/* Two phases: deleting while traversing the tree is not safe */
	for (handle = xtreeFirst(updcache); (handle != xtreeEnd(updcache)); handle = xtreeNext(updcache, handle)) {
		updcacheitem_t *cacheitem = (updcacheitem_t *)xtreeData(updcache, handle);
		if (strncasecmp(cacheitem->key, prefix, plen) != 0) continue;
		keys = (char **)realloc(keys, (nkeys+1) * sizeof(char *));
		keys[nkeys++] = cacheitem->key;
	}

	for (i = 0; (i < nkeys); i++) {
		handle = xtreeFind(updcache, keys[i]);
		if (handle != xtreeEnd(updcache)) {
			updcacheitem_t *cacheitem = (updcacheitem_t *)xtreeData(updcache, handle);
			int v;

			if (flushfirst && (cacheitem->valcount > 0)) {
				sprintf(filedir, "%s%s", rrddir, cacheitem->key);
				flush_cached_updates(cacheitem, NULL);
			}
			xtreeDelete(updcache, cacheitem->key);
			for (v = 0; (v < cacheitem->valcount); v++)
				if (cacheitem->vals[v]) xfree(cacheitem->vals[v]);
			free_owned_tpl(cacheitem);
			xfree(cacheitem->key);
			xfree(cacheitem);
		}
	}
	if (nkeys) dbgprintf("updcache: %s %d entries for host %s\n",
			     (flushfirst ? "flushed and dropped" : "discarded"), nkeys, hostname);
	if (keys) xfree(keys);

	if (nkeys) {
		/* Same tombstone economics as rrdcache_evict_idle: on the
		 * array-backed xtree a deleted slot (and its private key copy)
		 * is reclaimed only if the same key is re-added, and a dropped
		 * host that never returns would leak its slots forever. */
		void *rebuilt = xtreeNew(strcasecmp);

		for (handle = xtreeFirst(updcache); (handle != xtreeEnd(updcache)); handle = xtreeNext(updcache, handle)) {
			updcacheitem_t *cacheitem = (updcacheitem_t *)xtreeData(updcache, handle);
			xtreeAdd(rebuilt, cacheitem->key, cacheitem);
		}
		xtreeDestroy(updcache);
		updcache = rebuilt;
	}
}

void rrdcacheflushall(void)
{
	xtreePos_t handle;
	updcacheitem_t *cacheitem;

	if (updcache_keyofs == -1) return; /* No cache */

	for (handle = xtreeFirst(updcache); (handle != xtreeEnd(updcache)); handle = xtreeNext(updcache, handle)) {
		cacheitem = (updcacheitem_t *) xtreeData(updcache, handle);
		if (cacheitem->valcount > 0) {
			sprintf(filedir, "%s%s", rrddir, cacheitem->key);
			flush_cached_updates(cacheitem, NULL);
		}
	}
}

void rrdcacheflushhost(char *hostname)
{
	xtreePos_t handle;
	updcacheitem_t *cacheitem;
	flushtree_t *flushitem;
	int keylen;
	time_t now = gettimer();

	if (updcache_keyofs == -1) return;

	/* If we get a full path for the key, skip the leading rrddir */
	if (strncmp(hostname, rrddir, updcache_keyofs) == 0) hostname += updcache_keyofs;
	keylen = strlen(hostname);

	if (!have_flushtree) {
		flushtree = xtreeNew(strcasecmp);
		have_flushtree = 1;
	}
	handle = xtreeFind(flushtree, hostname);
	if (handle == xtreeEnd(flushtree)) {
		flushitem = (flushtree_t *)calloc(1, sizeof(flushtree_t));
		flushitem->hostname = strdup(hostname);
		flushitem->flushtime = 0;
		xtreeAdd(flushtree, flushitem->hostname, flushitem);
	}
	else {
		flushitem = (flushtree_t *) xtreeData(flushtree, handle);
	}

	if ((flushitem->flushtime + 60) >= now) {
		dbgprintf("Flush of '%s' skipped, too soon\n", hostname);
		return;
	}
	flushitem->flushtime = now;

	handle = xtreeFirst(updcache); 
	while (handle != xtreeEnd(updcache)) {
		cacheitem = (updcacheitem_t *) xtreeData(updcache, handle);

		switch (strncasecmp(cacheitem->key, hostname, keylen)) {
		  case 1 :
			handle = xtreeEnd(updcache); break;

		  case 0:
			if (cacheitem->valcount > 0) {
				dbgprintf("Flushing cache '%s'\n", cacheitem->key);
				sprintf(filedir, "%s%s", rrddir, cacheitem->key);
				flush_cached_updates(cacheitem, NULL);
			}
			/* Fall through */

		  default:
			handle = xtreeNext(updcache, handle);
			break;
		}
	}
}

static int rrddatasets(char *hostname, char ***dsnames)
{
	struct stat st;

	int result;
	xymon_rrd_argv_item_t fetch_params[] = { "rrdfetch", filedir, "AVERAGE", "-s", "-30m", NULL };
	time_t starttime, endtime;
	unsigned long steptime, dscount;
	rrd_value_t *rrddata;

	if ((size_t)snprintf(filedir, sizeof(filedir), "%s/%s/%s", rrddir, hostname, rrdfn) >= sizeof(filedir)) return 0;
	if (stat(filedir, &st) == -1) return 0;

	optind = opterr = 0; rrd_clear_error();
	result = xymon_rrd_fetch(5, fetch_params, &starttime, &endtime, &steptime, &dscount, dsnames, &rrddata);
	if (result == -1) {
		errprintf("Error while retrieving RRD dataset names from %s: %s\n",
			  filedir, rrd_get_error());
		return 0;
	}

	free(rrddata);	/* No use for the actual data */
	return dscount;
}

/* Include all of the sub-modules. */
#include "rrd/do_xymongen.c"
#include "rrd/do_xymonnet.c"
#include "rrd/do_xymonproxy.c"
#include "rrd/do_xymond.c"
#include "rrd/do_citrix.c"
#include "rrd/do_ntpstat.c"

#include "rrd/do_memory.c"	/* Must go before do_la.c */
#include "rrd/do_la.c"

/*
 * From hobbit-perl-client http://sourceforge.net/projects/hobbit-perl-cl/
 * version 1.15 Oct. 17 2006 (downloaded on 2008-12-01).
 *
 * Include file for netapp.pl dbcheck.pl and beastat.pl scripts
 * do_fd_lib.c contains some function used by the other library
 *
 * Must go before "do_disk.c"
 */
#include "rrd/do_fd_lib.c"
#include "rrd/do_netapp.c"
#include "rrd/do_beastat.c"
#include "rrd/do_dbcheck.c"


#include "rrd/do_disk.c"
#include "rrd/do_netstat.c"
#include "rrd/do_vmstat.c"
#include "rrd/do_iostat.c"
#include "rrd/do_ifstat.c"

#include "rrd/do_apache.c"
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

#include "rrd/do_ifmib.c"
#include "rrd/do_snmpmib.c"

/* z/OS, z/VM, z/VME stuff */
#include "rrd/do_paging.c"
#include "rrd/do_mdc.c"
#include "rrd/do_cics.c"
#include "rrd/do_getvis.c"
#include "rrd/do_asid.c"


/*
 * From devmon http://sourceforge.net/projects/devmon/
 * version 0.3.0 (downloaded on 2008-12-01).
 */
#include "rrd/do_devmon.c"


void update_rrd(char *hostname, char *testname, char *msg, time_t tstamp, char *sender, xymonrrd_t *ldef, char *classname, char *pagepaths)
{
	char *id;

	MEMDEFINE(rrdvalues);

	if (ldef) id = ldef->xymonrrdname; else id = testname;
	senderip = sender;

	/*
	 * Self-describing beats built-in: a status carrying a store block
	 * (XYMON METRICS / legacy DEVMON RRD) is written by the block
	 * writer, and ONLY by it - falling through to a built-in handler
	 * would write the same samples twice under two schemas. Emitting a
	 * block is deliberate: a column only reroutes when its producer
	 * chose to add one, so block-less statuses hit the exact same
	 * built-in chain as before.
	 */
	if      (xymon_markers_have_store(msg)) do_devmon_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	else if (strcmp(id, "bbgen") == 0)       do_xymongen_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "xymongen") == 0)    do_xymongen_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "bbtest") == 0)      do_xymonnet_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "xymonnet") == 0)    do_xymonnet_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "bbproxy") == 0)     do_xymonproxy_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "xymonproxy") == 0)  do_xymonproxy_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "hobbitd") == 0)     do_xymond_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "xymond") == 0)      do_xymond_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "citrix") == 0)      do_citrix_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "ntpstat") == 0)     do_ntpstat_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	else if (strcmp(id, "la") == 0)          do_la_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "disk") == 0)        do_disk_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "memory") == 0)      do_memory_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "netstat") == 0)     do_netstat_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "vmstat") == 0)      do_vmstat_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "iostat") == 0)      do_iostat_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "ifstat") == 0)      do_ifstat_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	/* These two come from the filerstats2bb.pl script. The reports are in disk-format */
	else if (strcmp(id, "inode") == 0)       do_disk_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "qtree") == 0)       do_disk_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	else if (strcmp(id, "apache") == 0)      do_apache_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "sendmail") == 0)    do_sendmail_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "mailq") == 0)       do_mailq_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "iishealth") == 0)   do_iishealth_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "temperature") == 0) do_temperature_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	else if (strcmp(id, "ncv") == 0)         do_ncv_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "tcp") == 0)         do_net_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	else if (strcmp(id, "filesizes") == 0)   do_filesizes_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "proccounts") == 0)  do_counts_rrd("processes", hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "portcounts") == 0)  do_counts_rrd("ports", hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "linecounts") == 0)  do_derives_rrd("lines", hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "deltacounts") == 0) do_counts_rrd("deltalines", hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "trends") == 0)      do_trends_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	else if (strcmp(id, "ifmib") == 0)       do_ifmib_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (is_snmpmib_rrd(id))             do_snmpmib_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	/* z/OS, z/VSE, z/VM from Rich Smrcina */
	else if (strcmp(id, "paging") == 0)      do_paging_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "mdc") == 0)         do_mdc_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "cics") == 0)        do_cics_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "getvis") == 0)      do_getvis_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "maxuser") == 0)     do_asid_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "nparts") == 0)      do_asid_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	/* 
	 * These are from the hobbit-perl-client
	 * NetApp check for netapp.pl, dbcheck.pl and beastat.pl scripts
	 */
	else if (strcmp(id, "xtstats") == 0)     do_netapp_extrastats_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "quotas") == 0)      do_disk_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "snapshot") == 0)    do_disk_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "TblSpace") == 0)    do_disk_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "stats") == 0)       do_netapp_stats_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "ops") == 0)         do_netapp_ops_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "cifs") == 0)        do_netapp_cifs_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "snaplist") == 0)    do_netapp_snaplist_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "snapmirr") == 0)    do_netapp_snapmirror_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "HitCache") == 0)    do_dbcheck_hitcache_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "Session") == 0)     do_dbcheck_session_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "RollBack") == 0)    do_dbcheck_rb_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "InvObj") == 0)      do_dbcheck_invobj_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "MemReq") == 0)      do_dbcheck_memreq_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "JVM") == 0)         do_beastat_jvm_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "JMS") == 0)         do_beastat_jms_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "JTA") == 0)         do_beastat_jta_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "ExecQueue") == 0)   do_beastat_exec_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	else if (strcmp(id, "JDBCConn") == 0)    do_beastat_jdbc_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	/*
	 * This is from the devmon SNMP collector
	 */
	else if (strcmp(id, "devmon") == 0)      do_devmon_rrd(hostname, testname, classname, pagepaths, msg, tstamp);

	else if (extids && exthandler) {
		int i;

		for (i=0; (extids[i] && strcmp(extids[i], id)); i++) ;

		if (extids[i]) do_external_rrd(hostname, testname, classname, pagepaths, msg, tstamp);
	}

	senderip = NULL;

	MEMUNDEFINE(rrdvalues);
}

