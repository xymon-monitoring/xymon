/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* test.cfg typed loader. See testcfg.h. Interprets the generic brace tree    */
/* (braceparse.h) into TEST -> METRIC -> backend structures.                  */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                           */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char testcfg_rcsid[] = "$Id$";

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "libxymon.h"

/* A word that is a known RRD storage verb rather than another backend block. */
static tc_backend_t *ensure_backend(tc_metric_t *m, const char *name)
{
	tc_backend_t *b;

	for (b = m->backends; (b && strcasecmp(b->name, name)); b = b->next) ;
	if (b) return b;
	b = (tc_backend_t *)xcalloc(1, sizeof(tc_backend_t));
	b->name = xstrdup(name);
	b->next = m->backends;
	m->backends = b;
	return b;
}

/* Apply one RRD-storage verb node (EXCLUDE / STOREPATTERN) to backend. */
static void apply_rrd_verb(tc_backend_t *b, bracenode_t *v)
{
	if ((strcasecmp(v->words[0], "EXCLUDE") == 0) && (v->nwords >= 2)) {
		if (b->excludepat) xfree(b->excludepat);
		b->excludepat = xstrdup(v->words[1]);
	}
	else if ((strcasecmp(v->words[0], "STOREPATTERN") == 0) && (v->nwords >= 2)) {
		if (b->storepat) xfree(b->storepat);
		b->storepat = xstrdup(v->words[1]);
	}
}

static int is_rrd_verb(const char *w)
{
	return (strcasecmp(w, "EXCLUDE") == 0) ||
	       (strcasecmp(w, "STOREPATTERN") == 0);
}

static void load_backend_block(tc_backend_t *b, bracenode_t *blk)
{
	int i;

	for (i = 0; i < blk->nchildren; i++) {
		bracenode_t *v = blk->children[i];
		if (v->nwords < 1) continue;
		if (is_rrd_verb(v->words[0]) && (strcasecmp(b->name, "rrd") == 0)) {
			apply_rrd_verb(b, v);
		}
		else {
			/* other backend (graphite, ...): keep raw words,
			 * NULL-terminated as testcfg.h promises */
			int j;
			for (j = 0; j < v->nwords; j++) {
				/* xrealloc refuses a NULL pointer: first growth mallocs */
				b->kv = (char **)(b->kv ? xrealloc(b->kv, (b->nkv + 2) * sizeof(char *))
							: xmalloc(2 * sizeof(char *)));
				b->kv[b->nkv++] = xstrdup(v->words[j]);
				b->kv[b->nkv] = NULL;
			}
		}
	}
}

static tc_metric_t *load_metric(bracenode_t *mnode)
{
	tc_metric_t *m;
	int i;

	/* A nameless METRIC would bind its test to an empty rrd name -
	 * a nonexistent handler, killing collection with no trace. Config
	 * errors fail loudly, never silently rebind. */
	if (mnode->nwords < 2) {
		errprintf("test.cfg: METRIC with no name at line %d - ignored\n", mnode->line);
		return NULL;
	}

	m = (tc_metric_t *)xcalloc(1, sizeof(tc_metric_t));
	m->name = xstrdup(mnode->words[1]);

	for (i = 0; i < mnode->nchildren; i++) {
		bracenode_t *c = mnode->children[i];
		if (c->nwords < 1) continue;

		if (strcasecmp(c->words[0], "TRACKMAX") == 0) m->trackmax = 1;
		else if (strcasecmp(c->words[0], "COUNTLINES") == 0) m->countlines = 1;
		else if ((strcasecmp(c->words[0], "NCV") == 0) || (strcasecmp(c->words[0], "SPLITNCV") == 0)) {
			/* Collect the "name:type" pairs into a comma-separated spec
			 * (the form the NCV env carries). Inline args ("NCV a:X b:Y")
			 * or a block body ("NCV { a:X; b:Y }") both work; a block
			 * entry's words rejoin on ':'. */
			strbuffer_t *sb = newstrbuffer(0);
			int j;
			for (j = 1; j < c->nwords; j++) {
				char *w = c->words[j];
				size_t wl = strlen(w);
				while (wl && (w[wl-1] == ',')) wl--;
				if (wl == 0) continue;
				if (STRBUFLEN(sb)) addtobuffer(sb, ",");
				addtobufferraw(sb, w, wl);
			}
			for (j = 0; j < c->nchildren; j++) {
				int k;
				bracenode_t *e = c->children[j];
				if (STRBUFLEN(sb)) addtobuffer(sb, ",");
				for (k = 0; k < e->nwords; k++) { if (k) addtobuffer(sb, ":"); addtobuffer(sb, e->words[k]); }
			}
			if (m->ncv) { xfree(m->ncv); m->ncv = NULL; }
			if (STRBUFLEN(sb) > 0) {
				m->ncv = xstrdup(STRBUF(sb));
				m->ncv_split = (strcasecmp(c->words[0], "SPLITNCV") == 0);
			}
			else {
				/* A bare NCV would otherwise override a working
				 * NCV_<col> env with an empty spec - loudly ignore. */
				errprintf("test.cfg: %s with no name:type pairs at line %d - ignored\n", c->words[0], c->line);
			}
			freestrbuffer(sb);
		}
		else if (is_rrd_verb(c->words[0])) {
			/* bare storage verb -> the implicit default (rrd) backend */
			apply_rrd_verb(ensure_backend(m, "rrd"), c);
		}
		else if (c->nchildren > 0) {
			/* a named backend block: RRD { ... } / GRAPHITE { ... } */
			char bname[64];
			int n = 0;
			while (c->words[0][n] && (n < 63)) { bname[n] = tolower((int)(unsigned char)c->words[0][n]); n++; }
			bname[n] = '\0';
			load_backend_block(ensure_backend(m, bname), c);
		}
	}
	return m;
}

static tc_test_t *load_test(bracenode_t *tnode)
{
	tc_test_t *t = (tc_test_t *)xcalloc(1, sizeof(tc_test_t));
	tc_metric_t *mtail = NULL;
	int i;

	t->name = xstrdup((tnode->nwords >= 2) ? tnode->words[1] : "");

	for (i = 0; i < tnode->nchildren; i++) {
		bracenode_t *c = tnode->children[i];
		if (c->nwords < 1) continue;

		if ((strcasecmp(c->words[0], "SOURCE") == 0) && (c->nwords >= 2)) { if (t->source) xfree(t->source); t->source = xstrdup(c->words[1]); }
		else if ((strcasecmp(c->words[0], "CMD") == 0) && (c->nwords >= 2)) { if (t->cmd) xfree(t->cmd); t->cmd = xstrdup(c->words[1]); }
		else if ((strcasecmp(c->words[0], "INTERVAL") == 0) && (c->nwords >= 2)) { if (t->interval) xfree(t->interval); t->interval = xstrdup(c->words[1]); }
		else if ((strcasecmp(c->words[0], "PORT") == 0) && (c->nwords >= 2)) { if (t->port) xfree(t->port); t->port = xstrdup(c->words[1]); }
		else if ((strcasecmp(c->words[0], "HANDLER") == 0) && (c->nwords >= 2)) { if (t->handler) xfree(t->handler); t->handler = xstrdup(c->words[1]); }
		else if (strcasecmp(c->words[0], "GRAPHS") == 0) {
			/* Comma-separated for the downstream consumer; tolerate both
			 * "GRAPHS a b" and "GRAPHS a, b" by stripping trailing commas. */
			strbuffer_t *sb = newstrbuffer(0);
			int j;
			for (j = 1; j < c->nwords; j++) {
				char *w = c->words[j];
				size_t wl = strlen(w);
				while (wl && (w[wl-1] == ',')) wl--;
				if (wl == 0) continue;
				if (STRBUFLEN(sb)) addtobuffer(sb, ",");
				addtobufferraw(sb, w, wl);
			}
			if (t->graphs) xfree(t->graphs);
			/* A bare "GRAPHS" (or all-comma args) is no override:
			 * keep the documented "verbatim, or NULL" contract
			 * instead of handing consumers an empty string. */
			t->graphs = (STRBUFLEN(sb) ? xstrdup(STRBUF(sb)) : NULL);
			freestrbuffer(sb);
		}
		else if (strcasecmp(c->words[0], "METRIC") == 0) {
			/* Append: the metric list keeps file order, so "the first
			 * metric that carries X" means the first one WRITTEN */
			tc_metric_t *m = load_metric(c);
			if (m) {
				if (mtail) mtail->next = m; else t->metrics = m;
				mtail = m;
			}
		}
	}
	return t;
}

tc_test_t *testcfg_parse(const char *text, char *errbuf, int errbufsz)
{
	bracenode_t *root;
	tc_test_t *head = NULL, *tail = NULL;
	int i;

	root = braceparse(text, errbuf, errbufsz);
	if (!root) return NULL;

	for (i = 0; i < root->nchildren; i++) {
		bracenode_t *c = root->children[i];
		tc_test_t *t;
		if ((c->nwords < 1) || strcasecmp(c->words[0], "TEST")) continue;
		t = load_test(c);
		if (tail) tail->next = t; else head = t;
		tail = t;
	}
	braceparse_free(root);
	return head;
}

static void free_backend(tc_backend_t *b)
{
	int i;
	while (b) {
		tc_backend_t *z = b; b = b->next;
		xfree(z->name);
		if (z->excludepat) xfree(z->excludepat);
		if (z->storepat) xfree(z->storepat);
		for (i = 0; i < z->nkv; i++) xfree(z->kv[i]);
		if (z->kv) xfree(z->kv);
		xfree(z);
	}
}

void testcfg_free(tc_test_t *head)
{
	while (head) {
		tc_test_t *t = head; head = head->next;
		tc_metric_t *m = t->metrics;
		while (m) {
			tc_metric_t *zm = m; m = m->next;
			xfree(zm->name);
			if (zm->ncv) xfree(zm->ncv);
			free_backend(zm->backends);
			xfree(zm);
		}
		xfree(t->name);
		if (t->source) xfree(t->source);
		if (t->cmd) xfree(t->cmd);
		if (t->interval) xfree(t->interval);
		if (t->port) xfree(t->port);
		if (t->handler) xfree(t->handler);
		if (t->graphs) xfree(t->graphs);
		xfree(t);
	}
}

tc_test_t *testcfg_load(void)
{
	static int loaded = 0;
	static tc_test_t *cache = NULL;
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf, *all;
	char err[200];

	if (loaded) return cache;
	loaded = 1;

	snprintf(fn, sizeof(fn), "%s/etc/test.cfg", xgetenv("XYMONHOME"));
	errno = 0;
	fd = stackfopen(fn, "r", NULL);
	if (fd == NULL) {
		/* A present-but-unreadable file must not be silently identical
		 * to an absent one: a permissions mistake would quietly turn
		 * the whole feature off. */
		if ((errno != 0) && (errno != ENOENT))
			errprintf("test.cfg exists but cannot be read (%s): %s - falling back to env settings\n", fn, strerror(errno));
		return NULL;
	}

	inbuf = newstrbuffer(0); all = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) addtobuffer(all, STRBUF(inbuf));
	stackfclose(fd);
	freestrbuffer(inbuf);

	err[0] = '\0';
	cache = testcfg_parse(STRBUF(all), err, sizeof(err));
	if (!cache && *err) errprintf("test.cfg: %s\n", err);
	freestrbuffer(all);
	return cache;
}

tc_test_t *testcfg_find(tc_test_t *head, const char *name)
{
	tc_test_t *t;
	/* Case-insensitive like the test->RRD tree (lib/xymonrrd.c), or a
	 * wrong-case section would change the RRD binding but silently miss
	 * the NCV/COUNTLINES overlays. */
	for (t = head; (t && strcasecmp(t->name, name)); t = t->next) ;
	return t;
}

tc_metric_t *testcfg_metric(tc_test_t *test, const char *name)
{
	tc_metric_t *m;
	if (!test) return NULL;
	for (m = test->metrics; (m && strcasecmp(m->name, name)); m = m->next) ;
	return m;
}

tc_backend_t *testcfg_backend(tc_metric_t *metric, const char *name)
{
	tc_backend_t *b;
	if (!metric) return NULL;
	if (!name) name = "rrd";
	for (b = metric->backends; (b && strcasecmp(b->name, name)); b = b->next) ;
	return b;
}

const char *testcfg_ncv(const char *testname, int *split)
{
	tc_test_t *t = testcfg_find(testcfg_load(), testname);
	tc_metric_t *m;

	if (!t) return NULL;
	for (m = t->metrics; (m); m = m->next) {
		if (m->ncv) {
			if (split) *split = m->ncv_split;
			return m->ncv;
		}
	}
	return NULL;
}

int testcfg_countlines(const char *testname)
{
	tc_test_t *t = testcfg_find(testcfg_load(), testname);
	tc_metric_t *m;

	if (!t) return 0;
	for (m = t->metrics; (m); m = m->next) if (m->countlines) return 1;
	return 0;
}

/* --- groups (the testgroup aggregator tier) --- */

static tc_group_t *load_group(bracenode_t *gnode)
{
	tc_group_t *g = (tc_group_t *)xcalloc(1, sizeof(tc_group_t));
	int i;

	g->name = xstrdup((gnode->nwords >= 2) ? gnode->words[1] : "");
	g->rollup = TC_ROLLUP_WORST;
	for (i = 0; i < gnode->nchildren; i++) {
		bracenode_t *c = gnode->children[i];
		if (c->nwords < 1) continue;
		if (strcasecmp(c->words[0], "member") == 0) {
			int j;
			/* One line may list several members: "member a b c". */
			for (j = 1; j < c->nwords; j++) {
				size_t sz = (g->nmembers + 1) * sizeof(char *);
				g->members = (char **)(g->members ? xrealloc(g->members, sz) : xmalloc(sz));
				g->members[g->nmembers++] = xstrdup(c->words[j]);
			}
		}
		else if ((strcasecmp(c->words[0], "rollup") == 0) && (c->nwords >= 2)) {
			/* Only "worst" is defined today; anything else keeps the default. */
			g->rollup = TC_ROLLUP_WORST;
		}
	}
	return g;
}

tc_group_t *testcfg_groups_parse(const char *text, char *errbuf, int errbufsz)
{
	bracenode_t *root;
	tc_group_t *head = NULL, *tail = NULL;
	int i;

	root = braceparse(text, errbuf, errbufsz);
	if (!root) return NULL;
	for (i = 0; i < root->nchildren; i++) {
		bracenode_t *c = root->children[i];
		tc_group_t *g;
		if ((c->nwords < 1) || strcasecmp(c->words[0], "group")) continue;
		g = load_group(c);
		if (tail) tail->next = g; else head = g;
		tail = g;
	}
	braceparse_free(root);
	return head;
}

void testcfg_groups_free(tc_group_t *head)
{
	while (head) {
		tc_group_t *g = head; head = head->next;
		int i;
		xfree(g->name);
		for (i = 0; i < g->nmembers; i++) xfree(g->members[i]);
		if (g->members) xfree(g->members);
		xfree(g);
	}
}

tc_group_t *testcfg_group_of(tc_group_t *head, const char *test)
{
	tc_group_t *g;
	int i;

	if (!test) return NULL;
	for (g = head; (g); g = g->next)
		for (i = 0; i < g->nmembers; i++)
			if (strcasecmp(g->members[i], test) == 0) return g;
	return NULL;
}

tc_group_t *testcfg_groups(void)
{
	static int loaded = 0;
	static tc_group_t *cache = NULL;
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf, *all;
	char err[200];

	if (loaded) return cache;
	loaded = 1;

	snprintf(fn, sizeof(fn), "%s/etc/test.cfg", xgetenv("XYMONHOME"));
	errno = 0;
	fd = stackfopen(fn, "r", NULL);
	if (fd == NULL) return NULL;

	inbuf = newstrbuffer(0); all = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) addtobuffer(all, STRBUF(inbuf));
	stackfclose(fd);
	freestrbuffer(inbuf);

	err[0] = '\0';
	cache = testcfg_groups_parse(STRBUF(all), err, sizeof(err));
	if (!cache && *err) errprintf("test.cfg: %s\n", err);
	freestrbuffer(all);
	return cache;
}
