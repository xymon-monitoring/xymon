/* test.cfg braced-config parser: parse the RFC's worked example and assert
 * the TEST -> METRIC -> backend model, plus grammar edge cases (inline vs
 * block bodies, comments, quotes, ';' vs newline, nested backends). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static int failures = 0;

static void ck(const char *label, int cond)
{
	if (!cond) { fprintf(stderr, "FAIL: %s\n", label); failures++; }
}

static const char *CFG =
	"# a comment line\n"
	"TEST cpu    { SOURCE client; METRIC la }\n"
	"TEST disk   { SOURCE client; METRIC disk { COUNTLINES } }\n"
	"TEST smtp {\n"
	"        SOURCE network\n"
	"        PORT   25\n"
	"        METRIC tcp\n"
	"}\n"
	"TEST diskio {\n"
	"        SOURCE   script\n"
	"        CMD      \"/path with spaces/diskio.py\"   # quoted, has spaces\n"
	"        INTERVAL 5m\n"
	"        METRIC diskio_ops     { EXCLUDE loop }\n"
	"        METRIC diskio_busy    { STOREPATTERN ,root }\n"
	"        METRIC diskio_total {\n"
	"                RRD      { EXCLUDE tmp }\n"
	"                GRAPHITE { PREFIX servers.disk }\n"
	"        }\n"
	"}\n"
	"TEST storage {\n"
	"        SOURCE  script\n"
	"        HANDLER markers\n"
	"        GRAPHS  storage_io\n"
	"        METRIC  storage_io { EXCLUDE none }\n"
	"}\n"
	"TEST trends { SOURCE server }\n"
	"TEST oops {\n"
	"        SOURCE client\n"
	"        METRIC\n"
	"        METRIC good\n"
	"}\n";

int main(void)
{
	char err[200];
	tc_test_t *tests, *t;
	tc_metric_t *m;
	tc_backend_t *b;

	tests = testcfg_parse(CFG, err, sizeof(err));
	ck("parse succeeds", tests != NULL);
	if (!tests) { fprintf(stderr, "parse error: %s\n", err); return 1; }

	/* cpu: inline block, one metric, no storage */
	t = testcfg_find(tests, "cpu");
	ck("cpu found", t != NULL);
	ck("cpu source client", t && t->source && strcmp(t->source, "client") == 0);
	ck("cpu has metric la", testcfg_metric(t, "la") != NULL);

	/* disk: metric with a COUNTLINES flag */
	t = testcfg_find(tests, "disk");
	m = testcfg_metric(t, "disk");
	ck("disk.disk countlines", m && m->countlines);

	/* smtp: block-body verbs, port kept */
	t = testcfg_find(tests, "smtp");
	ck("smtp source network", t && t->source && strcmp(t->source, "network") == 0);
	ck("smtp port 25", t && t->port && strcmp(t->port, "25") == 0);
	ck("smtp metric tcp", testcfg_metric(t, "tcp") != NULL);

	/* diskio: quoted CMD with spaces; per-metric storage on the default backend */
	t = testcfg_find(tests, "diskio");
	ck("diskio quoted cmd", t && t->cmd && strcmp(t->cmd, "/path with spaces/diskio.py") == 0);
	ck("diskio interval", t && t->interval && strcmp(t->interval, "5m") == 0);

	m = testcfg_metric(t, "diskio_ops");
	b = testcfg_backend(m, NULL);   /* default rrd */
	ck("ops backend present", b != NULL);
	ck("ops exclude loop", b && b->excludepat && strcmp(b->excludepat, "loop") == 0);

	m = testcfg_metric(t, "diskio_busy");
	b = testcfg_backend(m, NULL);
	ck("busy storepattern ,root", b && b->storepat && strcmp(b->storepat, ",root") == 0);
	ck("busy has no exclude", b && b->excludepat == NULL);

	/* diskio_total: two explicit backend blocks */
	m = testcfg_metric(t, "diskio_total");
	ck("total has rrd backend", testcfg_backend(m, "rrd") != NULL);
	b = testcfg_backend(m, "graphite");
	ck("total has graphite backend", b != NULL);
	ck("total graphite raw kv kept", b && b->nkv >= 2 &&
	   strcasecmp(b->kv[0], "PREFIX") == 0 && strcmp(b->kv[1], "servers.disk") == 0);

	/* oops: a nameless METRIC is rejected loudly, the rest of the test loads */
	t = testcfg_find(tests, "oops");
	ck("oops found", t != NULL);
	ck("oops keeps its named metric", testcfg_metric(t, "good") != NULL);
	ck("oops has exactly one metric", t && t->metrics && (t->metrics->next == NULL));

	/* storage: overrides captured */
	t = testcfg_find(tests, "storage");
	ck("storage handler override", t && t->handler && strcmp(t->handler, "markers") == 0);
	ck("storage graphs override", t && t->graphs && strcmp(t->graphs, "storage_io") == 0);

	/* trends: pseudo-column, no metrics */
	t = testcfg_find(tests, "trends");
	ck("trends source server", t && t->source && strcmp(t->source, "server") == 0);
	ck("trends has no metrics", t && t->metrics == NULL);

	testcfg_free(tests);

	/* Syntax error: unbalanced brace is reported, not crashed. */
	ck("unbalanced brace fails", testcfg_parse("TEST x { SOURCE client", err, sizeof(err)) == NULL);

	/* Silent-loss regressions: each of these must FAIL LOUDLY (NULL
	 * result + populated errbuf), never quietly drop config. */
	err[0] = '\0';
	ck("stray top-level } after words fails",
	   testcfg_parse("TEST a { SOURCE client }\nfoo }\nTEST b { SOURCE client }", err, sizeof(err)) == NULL);
	ck("stray top-level } is reported", err[0] != '\0');
	err[0] = '\0';
	ck("unterminated quote fails",
	   testcfg_parse("CMD \"oops\nTEST b { SOURCE client }", err, sizeof(err)) == NULL);
	ck("unterminated quote is reported", strstr(err, "quote") != NULL);

	/* Metric order: the FIRST metric in file order that carries an NCV
	 * spec wins - the list must not be reversed by the loader. */
	tests = testcfg_parse(
		"TEST multi {\n"
		"  METRIC m1 { NCV a:GAUGE }\n"
		"  METRIC m2 { NCV b:GAUGE }\n"
		"}\n", err, sizeof(err));
	t = testcfg_find(tests, "multi");
	ck("metric list keeps file order", t && t->metrics && strcmp(t->metrics->name, "m1") == 0);
	testcfg_free(tests);

	/* A bare NCV (no pairs) is absent, not an empty spec that would
	 * override a working NCV_<col> env with nothing. */
	tests = testcfg_parse("TEST bare { METRIC bare { NCV } }", err, sizeof(err));
	t = testcfg_find(tests, "bare");
	ck("bare NCV is absent", t && testcfg_metric(t, "bare") && testcfg_metric(t, "bare")->ncv == NULL);
	testcfg_free(tests);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	/* --- groups (the testgroup tier) --- */
	{
		tc_group_t *groups, *g;

		groups = testcfg_groups_parse(
			"TEST smarttemp { SOURCE client }\n"	/* TEST sections are ignored here */
			"group smart {\n"
			"        member smarttemp\n"
			"        member smartsata\n"
			"        rollup worst\n"
			"}\n"
			"group net { member ping http }\n", err, sizeof(err));
		ck("groups parse", groups != NULL);
		g = testcfg_group_of(groups, "smarttemp");
		ck("test resolves to its group", g && (strcmp(g->name, "smart") == 0));
		ck("group keeps both members", g && (g->nmembers == 2));
		ck("member order preserved", g && (strcmp(g->members[0], "smarttemp") == 0) && (strcmp(g->members[1], "smartsata") == 0));
		ck("rollup defaults to worst", g && (g->rollup == TC_ROLLUP_WORST));
		ck("one line, several members", testcfg_group_of(groups, "http") && (strcmp(testcfg_group_of(groups, "http")->name, "net") == 0));
		ck("non-member test -> NULL", testcfg_group_of(groups, "nobody") == NULL);
		ck("a TEST section is not a group", testcfg_groups_parse("TEST solo { SOURCE client }\n", err, sizeof(err)) == NULL);
		testcfg_groups_free(groups);
	}

	return failures ? 1 : 0;
}
