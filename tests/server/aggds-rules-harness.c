/* AGGDS aggregate rules: sum/avg/max/min/count over the latest stored
 * values of a metric set, evaluated per host with first-match shadowing
 * and a freshness window. Drives the real analysis.cfg parser, store and
 * checker from client_config.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utime.h>

#include "libxymon.h"
#include "client_config.h"

/* load_client_config() skips the reload when the previously loaded file
 * is unmodified - bump a file's mtime into the future to defeat that. */
static void touch_future(const char *path, int offset)
{
	struct utimbuf t;

	t.actime = t.modtime = time(NULL) + offset;
	utime(path, &t);
}

/* Not in client_config.h - the idle-eviction sweep is driven by the
 * hourly housekeeping inside check_aggds_thresholds(); linking against
 * client_config.c lets the test call it with its own horizon. */
extern int aggds_evict_idle(time_t maxage);

static int failures = 0;

static void expect(const char *label, const char *text, const char *needle, int want)
{
	int have = (text && strstr(text, needle) != NULL);

	if (have != want) {
		fprintf(stderr, "%s: '%s' %s in:\n%s\n", label, needle,
			(want ? "missing" : "unexpected"), (text ? text : "(null)"));
		failures++;
	}
}

static void expectn(const char *label, int have, int want)
{
	if (have != want) {
		fprintf(stderr, "%s: got %d, want %d\n", label, have, want);
		failures++;
	}
}

/* Build a dsnames tree the way setup_template() does */
static void *mktree(const char *ds1, const char *ds2)
{
	void *tree = xtreeNew(strcmp);
	rrdtplnames_t *tpl;

	tpl = (rrdtplnames_t *)calloc(1, sizeof(rrdtplnames_t));
	tpl->dsnam = strdup(ds1); tpl->idx = 1;
	xtreeAdd(tree, tpl->dsnam, tpl);
	if (ds2) {
		tpl = (rrdtplnames_t *)calloc(1, sizeof(rrdtplnames_t));
		tpl->dsnam = strdup(ds2); tpl->idx = 2;
		xtreeAdd(tree, tpl->dsnam, tpl);
	}
	return tree;
}

int main(void)
{
	void *opstree = mktree("reads", "writes");
	char vals[128];
	time_t now = getcurrenttime(NULL);
	strbuffer_t *res;
	char *out;

	{
		/* "!" forces a file load - a bare $HOSTSCFG path detours
		 * through a xymond network query first */
		char hostsfn[1024];
		snprintf(hostsfn, sizeof(hostsfn), "!%s", getenv("HOSTSCFG"));
		load_hostnames(hostsfn, NULL, get_fqdn());
	}
	load_client_config(NULL);

	/* Three fresh instances: reads 10+5+118=133, writes 20+6+302=328 */
	snprintf(vals, sizeof(vals), "%d:10:20", (int)now);
	update_aggds_store("testhost", "diskio_ops.ada0.rrd", opstree, vals);
	snprintf(vals, sizeof(vals), "%d:5:6", (int)now);
	update_aggds_store("testhost", "diskio_ops.ada1.rrd", opstree, vals);
	snprintf(vals, sizeof(vals), "%d:118:302", (int)now);
	update_aggds_store("testhost", "diskio_ops.da0.rrd", opstree, vals);
	/* A stale instance, last updated an hour ago: excluded everywhere */
	snprintf(vals, sizeof(vals), "%d:1000:1000", (int)(now - 3600));
	update_aggds_store("testhost", "diskio_ops.gone.rrd", opstree, vals);
	/* An unrelated file that must not match the pattern */
	snprintf(vals, sizeof(vals), "%d:5000:5000", (int)now);
	update_aggds_store("testhost", "other_metric.x.rrd", opstree, vals);

	res = check_aggds_thresholds("testhost", "linux", "/");
	out = (res ? STRBUF(res) : NULL);

	/* sum(reads)=133 > 100 -> yellow; the stale and unrelated values
	 * (1000/5000) must not be inside, or the sum would differ. The
	 * modify source carries the file pattern: the aggregate's identity
	 * is fn(pattern:ds), not fn(ds). */
	expect("sum over fresh matching values", out, "modify testhost.diskio yellow aggds:sum(%diskio_ops\\..+\\.rrd:reads):yellow Total reads high: 133.00", 1);
	/* A second aggregate on the SAME column gets its own modify source,
	 * so the two do not clobber each other in xymond's modifier list */
	expect("per-aggregate modify source", out, "modify testhost.diskio yellow aggds:max(%diskio_ops\\..+\\.rrd:reads):yellow Max read 118.00", 1);
	/* count(reads)=3 (stale excluded), rule is < 3 -> no red modify */
	expect("count excludes stale instances", out, "modify testhost.diskio red", 0);
	/* max(writes)=302 > 250 -> yellow on another column, default text
	 * (&N is the pattern-qualified aggregate name) */
	expect("max with default status text", out, "modify testhost.diskio2 yellow aggds:max(%diskio_ops\\..+\\.rrd:writes):yellow max(%diskio_ops\\..+\\.rrd:writes)=302.00 (> 250.00)", 1);
	/* Two severities on the SAME aggregate: both fire, and each keeps
	 * its own modifier slot. With a severity-less shared source, xymond's
	 * last-wins handle_modify would let the later yellow overwrite the
	 * red modifier - a silent downgrade of the verdict. */
	expect("red keeps its own modifier slot", out, "modify testhost.diskio5 red aggds:count(%diskio_ops\\..+\\.rrd:reads):red crit few", 1);
	expect("yellow keeps its own modifier slot", out, "modify testhost.diskio5 yellow aggds:count(%diskio_ops\\..+\\.rrd:reads):yellow warn few", 1);
	/* avg(reads)=44.33 not > 100 -> nothing for diskio3 */
	expect("avg below threshold stays quiet", out, "diskio3", 0);
	/* first-match shadowing: the second, tighter sum rule for the same
	 * column+aggregate+color is shadowed even though it would match */
	expect("first match wins per column/aggregate/color", out, "SHADOWED", 0);
	/* Same fn and ds over DIFFERENT filesets are different aggregates:
	 * neither shadows the other, and each gets its own modify source */
	expect("different patterns are different aggregates", out, "ada sum 15.00", 1);
	expect("different patterns are different aggregates", out, "da sum 118.00", 1);
	/* A malformed AGGDS line (missing :dataset) must not wipe the active
	 * rule-section criteria: the "scoped" rule that follows it stays
	 * bound to HOST=scopedhost, so it must not fire for testhost. */
	expect("malformed AGGDS keeps section criteria", out, "scoped fired", 0);
	/* An unparseable threshold (">notanumber") must not arm the rule
	 * with the failed-strtod 0.0 - i.e. never behave as ">0" */
	expect("garbage threshold cannot fire", out, "garbage limit fired", 0);

	/* ...and the section-scoped rule still works where it belongs */
	snprintf(vals, sizeof(vals), "%d:50:1", (int)now);
	update_aggds_store("scopedhost", "diskio_ops.sda0.rrd", opstree, vals);
	res = check_aggds_thresholds("scopedhost", "linux", "/");
	out = (res ? STRBUF(res) : NULL);
	expect("section rule fires for its own host", out, "scoped fired: 50.00", 1);
	expect("garbage threshold cannot fire", out, "garbage limit fired", 0);

	/* A host with no stored values: no result at all */
	res = check_aggds_thresholds("otherhost", "linux", "/");
	expect("no data, no modifies", (res ? STRBUF(res) : NULL), "modify", 0);

	/* Dropping a host empties its slice of the store: value aggregates
	 * vanish - but count() MUST evaluate the empty fileset as 0 and
	 * fire, because "the instances disappeared" is its primary use. */
	flush_aggds_store("testhost");
	res = check_aggds_thresholds("testhost", "linux", "/");
	out = (res ? STRBUF(res) : NULL);
	expect("flushed host: value aggregates vanish", out, "Total reads high", 0);
	expect("flushed host: count fires on the empty fileset", out, "modify testhost.diskio red aggds:count(%diskio_ops\\..+\\.rrd:reads):red Disks missing: only 0.00 reporting", 1);
	snprintf(vals, sizeof(vals), "%d:200:1", (int)now);
	update_aggds_store("testhost", "diskio_ops.ada0.rrd", opstree, vals);
	res = check_aggds_thresholds("testhost", "linux", "/");
	expect("store repopulates after a flush", (res ? STRBUF(res) : NULL), "Total reads high: 200.00", 1);

	/* A reload that removes every AGGDS rule tears the store down; a
	 * later reload with rules starts from scratch and works. */
	{
		char cfgpath[1024];
		snprintf(cfgpath, sizeof(cfgpath), "%s/etc/analysis.cfg", getenv("XYMONHOME"));
		touch_future(cfgpath, 10);
	}
	load_client_config(getenv("EMPTYCFG"));
	update_aggds_store("testhost", "diskio_ops.ada0.rrd", opstree, vals);
	res = check_aggds_thresholds("testhost", "linux", "/");
	expect("no rules, no store, no modifies", (res ? STRBUF(res) : NULL), "modify", 0);
	touch_future(getenv("EMPTYCFG"), 20);
	load_client_config(NULL);
	snprintf(vals, sizeof(vals), "%d:150:1", (int)now);
	update_aggds_store("testhost", "diskio_ops.ada0.rrd", opstree, vals);
	res = check_aggds_thresholds("testhost", "linux", "/");
	expect("store rebuilt after rules return", (res ? STRBUF(res) : NULL), "Total reads high: 150.00", 1);

	/* Idle eviction: churned instances (rotating container mounts) leave
	 * store entries nothing else deletes. The sweep frees any entry idle
	 * past the horizon - one per (file, dataset), so a two-DS update is
	 * two entries - and really deletes it: a second sweep finds nothing. */
	snprintf(vals, sizeof(vals), "%d:1:1", (int)(now - 7200));
	update_aggds_store("testhost", "diskio_ops.churned.rrd", opstree, vals);
	expectn("idle entries are evicted", aggds_evict_idle(3600), 2);
	expectn("eviction deletes, not skips", aggds_evict_idle(3600), 0);
	/* ... and the same key re-enters cleanly through the rebuilt tree */
	snprintf(vals, sizeof(vals), "%d:1:1", (int)(now - 7200));
	update_aggds_store("testhost", "diskio_ops.churned.rrd", opstree, vals);
	expectn("evicted keys can re-enter the store", aggds_evict_idle(3600), 2);
	/* fresh survivors are untouched by the sweeps above */
	res = check_aggds_thresholds("testhost", "linux", "/");
	expect("fresh entries survive the sweep", (res ? STRBUF(res) : NULL), "Total reads high: 150.00", 1);


	/* Warm-up guard: the store is memory-only, the fileset index is
	 * durable. When the index knows more fresh matching instances than
	 * the store has re-seen values for (= just after a restart/reload),
	 * count() must skip - not fire a false "instances disappeared". */
	{
		char flatdir[1024];
		const char *xh = getenv("XYMONHOME");
		snprintf(flatdir, sizeof(flatdir), "%s/rrdflat", (xh ? xh : "."));
		fsidx_set_dsnames("reads,writes");
		fsidx_note_schema(flatdir, "testhost", "diskio_ops.warm.rrd", getcurrenttime(NULL));
		fsidx_set_dsnames(NULL);
	}
	flush_aggds_store("testhost");
	res = check_aggds_thresholds("testhost", "linux", "/");
	out = (res ? STRBUF(res) : NULL);
	expect("restart warm-up: count skips while the index knows more", out, "Disks missing", 0);

	/* Once the store has re-seen a value for every countable instance
	 * the census no longer exceeds it: the guard lifts inside the
	 * warm-up window and count() evaluates the real fileset (the one
	 * re-seen file = 1). */
	snprintf(vals, sizeof(vals), "%d:6:1", (int)now);
	update_aggds_store("testhost", "diskio_ops.warm.rrd", opstree, vals);
	res = check_aggds_thresholds("testhost", "linux", "/");
	out = (res ? STRBUF(res) : NULL);
	expect("warm-up guard lifts once the census is covered", out, "Disks missing: only 1.00 reporting", 1);

	/* An index entry with no d= names (legacy handlers record none)
	 * cannot be tied to any dataset: it must not inflate the census, or
	 * every rule would sit silently skipped for the whole window. */
	{
		char flatdir[1024];
		const char *xh = getenv("XYMONHOME");
		snprintf(flatdir, sizeof(flatdir), "%s/rrdflat", (xh ? xh : "."));
		fsidx_note_commit(flatdir, "testhost", "diskio_ops.legacy.rrd", getcurrenttime(NULL));
	}
	res = check_aggds_thresholds("testhost", "linux", "/");
	out = (res ? STRBUF(res) : NULL);
	expect("no-d= index entry does not inflate the census", out, "Disks missing: only 1.00 reporting", 1);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
