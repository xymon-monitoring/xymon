/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* test.cfg: the test registry. A test is the status unit - one section per   */
/* test, holding how it is measured (SOURCE/CMD/...), the METRICs it produces */
/* (each with per-metric, per-backend storage policy), and overrides for the  */
/* derived defaults (HANDLER, GRAPHS). Parsed from the brace-config grammar    */
/* (see braceparse.h). This layer is the typed model + loader, overlaid onto  */
/* the env tables by htmllog, xymonrrd and do_ncv.                            */
/*                                                                            */
/* Lifetime: the file is read ONCE per process (at first use) and never       */
/* re-stat'ed - xymond_rrd and the CGIs must be restarted to pick up edits.   */
/* Unlike the env-derived tables (rebuilt every 5 minutes), and the same as   */
/* graphs.cfg's writer-side keywords. Deliberate for now; revisit if test.cfg */
/* ever carries settings an admin expects to hot-reload.                      */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                           */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __TESTCFG_H__
#define __TESTCFG_H__

/* Storage policy for one metric on one backend. RRD is the implicit default;
 * bare storage verbs under a METRIC target it. Only RRD keywords are modelled
 * for now; other backends (GRAPHITE, ...) keep their verbs as raw name/value. */
typedef struct tc_backend_t {
	char *name;			/* "rrd", "graphite", ... */
	int lazy;			/* RRD: LAZY */
	char *excludepat;		/* RRD: EXCLUDE <re> */
	char *storepat;			/* RRD: STOREPATTERN <re> */
	char **kv;			/* other backends: raw "KEYWORD arg" words, NULL-terminated */
	int nkv;
	struct tc_backend_t *next;
} tc_backend_t;

typedef struct tc_metric_t {
	char *name;
	int trackmax;			/* TRACKMAX */
	int countlines;			/* COUNTLINES (paging from status lines) */
	char *ncv;			/* NCV/SPLITNCV spec (name:type ...), or NULL */
	int ncv_split;			/* the spec came from SPLITNCV (one file per var) */
	tc_backend_t *backends;		/* at least the implicit "rrd" if any storage verb seen */
	struct tc_metric_t *next;
} tc_metric_t;

typedef struct tc_test_t {
	char *name;			/* == the status test */
	char *source;			/* client | network | script | server | ... */
	char *cmd;			/* SOURCE script: command */
	char *interval;			/* e.g. "5m" */
	char *port;			/* SOURCE network */
	char *handler;			/* override: forced parse handler, or NULL */
	char *graphs;			/* override: status-page graph list (verbatim), or NULL */
	tc_metric_t *metrics;
	struct tc_test_t *next;
} tc_test_t;

/* Parse text (already read from test.cfg) into the model. Returns NULL and
 * fills errbuf on a syntax error. Free with testcfg_free(). */
extern tc_test_t *testcfg_parse(const char *text, char *errbuf, int errbufsz);
extern void testcfg_free(tc_test_t *head);

/* Load $XYMONHOME/etc/test.cfg once and cache it (NULL if absent or on a
 * syntax error, which is logged). Shared by every consumer that overlays
 * test.cfg on its environment fallback. */
extern tc_test_t *testcfg_load(void);

/* Lookups. */
extern tc_test_t *testcfg_find(tc_test_t *head, const char *name);
extern tc_metric_t *testcfg_metric(tc_test_t *test, const char *name);
extern tc_backend_t *testcfg_backend(tc_metric_t *metric, const char *name);  /* name NULL -> the default (rrd) */

/* The NCV/SPLITNCV parse spec for a test (the first metric of the test that
 * carries one), or NULL. *split (if non-NULL) is set to 1 for SPLITNCV. Used
 * by the RRD writer to overlay the NCV_<col>/SPLITNCV_<col> environment. */
extern const char *testcfg_ncv(const char *testname, int *split);

/* Does any metric of this test carry COUNTLINES? Then the status page
 * derives its graph-paging count from the status lines (the --multigraphs
 * membership), regardless of the built-in/env list. */
extern int testcfg_countlines(const char *testname);

/* --- The testgroup tier: a display-only aggregator over member tests. --- */

/* Rollup rule for a group's aggregate colour. */
#define TC_ROLLUP_WORST 0

/* One "group <name> { member <test>... ; rollup worst }" section. Members
 * stay independent status tests; the group governs only how they are shown
 * and rolled up (the aggregator light + the merged detail page). */
typedef struct tc_group_t {
	char *name;
	char **members;			/* member test names, in file order */
	int nmembers;
	int rollup;			/* TC_ROLLUP_* (worst for now) */
	struct tc_group_t *next;
} tc_group_t;

/* Parse the group sections from test.cfg text (pure; free with the below). */
extern tc_group_t *testcfg_groups_parse(const char *text, char *errbuf, int errbufsz);
extern void testcfg_groups_free(tc_group_t *head);
/* Load + cache the groups from $XYMONHOME/etc/test.cfg (NULL if none). */
extern tc_group_t *testcfg_groups(void);
/* The group a test belongs to, or NULL. Case-insensitive, like testcfg_find. */
extern tc_group_t *testcfg_group_of(tc_group_t *head, const char *test);

#endif
