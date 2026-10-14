/*
 * web/dsidx.inc.c
 *
 * Parse-time expansion of the graphs.cfg @DSIDX@ family (@DSIDX@,
 * @PREVDSIDX@, @DSSTART:N@) and the DEF-line helpers it needs.
 *
 * #include'd by web/showgraph.c and by tests/web/dsidx-harness.c, so the
 * test drives this code rather than a copy of it.
 *
 * The including TU must provide gdef_t with the fields defs, dscount and
 * dsidx_runtime, and the xfree() macro.
 */

/* Replace all occurrences of `needle` in `src` with `repl`. Returns a
 * malloc'd string; caller frees. */
static char *str_replace_all(const char *src, const char *needle, const char *repl)
{
	int nlen = strlen(needle), rlen = strlen(repl);
	const char *p;
	char *out, *q;
	int count = 0;

	for (p = src; (p = strstr(p, needle)) != NULL; p += nlen) count++;
	out = (char *)malloc(strlen(src) + count * (rlen - nlen) + 1);

	q = out;
	while ((p = strstr(src, needle)) != NULL) {
		int prefix = p - src;
		memcpy(q, src, prefix); q += prefix;
		memcpy(q, repl, rlen); q += rlen;
		src = p + nlen;
	}
	memcpy(q, src, strlen(src) + 1);
	return out;
}

/* Classify a def line for @DSIDX@ expansion.
 *   *body  - the line content after any @DSSTART:N@ prefix is stripped
 *   *start - lower bound of the index loop:
 *              * explicit @DSSTART:N@ prefix wins
 *              * else 2 if the line uses @PREVDSIDX@ (skips ping0)
 *              * else 1
 * Returns 1 if the line should be looped, 0 if it should be emitted once. */
static int classify_dsidx_line(char *line, char **body, int *start)
{
	*body = line;
	*start = 1;

	if (strncmp(line, "@DSSTART:", 9) == 0) {
		char *p = line + 9;
		int s = atoi(p);
		while (isdigit((int)*p)) p++;
		if ((*p == '@') && (s > 0)) {
			*start = s;
			*body = p + 1;
		}
	}

	if (strstr(*body, "@DSIDX@") || strstr(*body, "@PREVDSIDX@")) {
		if (strstr(*body, "@PREVDSIDX@") && (*start < 2)) *start = 2;
		return 1;
	}
	return 0;
}

/* Expand every @DSIDX@/@PREVDSIDX@-templated def line in `defs` into N
 * concrete copies, returning a freshly-allocated NULL-terminated array.
 * Non-templated lines are copied verbatim. Caller owns the result.
 * If n <= 0 the templates pass through unchanged so the literal tokens
 * reach rrdtool, which errors loudly -- the desired fail-fast signal. */
static char **expand_dsidx_array(char *const *defs, int n)
{
	int i, newcount = 0, outi = 0;
	char **newdefs;
	char idxstr[16], previdxstr[16];

	if (defs == NULL) return NULL;

	if (n <= 0) {
		for (i = 0; defs[i]; i++) newcount++;
		newdefs = (char **)calloc(newcount + 1, sizeof(char *));
		for (i = 0; defs[i]; i++) newdefs[i] = strdup(defs[i]);
		newdefs[newcount] = NULL;
		return newdefs;
	}

	for (i = 0; defs[i]; i++) {
		char *body;
		int start;
		char *line = strdup(defs[i]);
		if (classify_dsidx_line(line, &body, &start)) {
			int m = n - start + 1;
			newcount += (m > 0 ? m : 0);
		}
		else {
			newcount++;
		}
		xfree(line);
	}

	newdefs = (char **)calloc(newcount + 1, sizeof(char *));
	for (i = 0; defs[i]; i++) {
		char *body;
		int start;
		char *line = strdup(defs[i]);
		if (classify_dsidx_line(line, &body, &start)) {
			int idx;
			for (idx = start; idx <= n; idx++) {
				char *tmp;
				snprintf(idxstr, sizeof(idxstr), "%d", idx);
				snprintf(previdxstr, sizeof(previdxstr), "%d", idx - 1);
				/* Replace @PREVDSIDX@ first so we don't accidentally chew the
				 * shorter @DSIDX@ inside it (currently they don't share a
				 * prefix, but this keeps the substitution order intent-clear). */
				tmp = str_replace_all(body, "@PREVDSIDX@", previdxstr);
				newdefs[outi++] = str_replace_all(tmp, "@DSIDX@", idxstr);
				xfree(tmp);
			}
		}
		else {
			newdefs[outi++] = strdup(defs[i]);
		}
		xfree(line);
	}
	newdefs[outi] = NULL;
	return newdefs;
}

/* Does any def line in `defs` use @DSIDX@/@PREVDSIDX@? */
static int defs_use_dsidx(char *const *defs)
{
	int i;
	if (defs == NULL) return 0;
	for (i = 0; defs[i]; i++) {
		if (strstr(defs[i], "@DSIDX@") || strstr(defs[i], "@PREVDSIDX@")) return 1;
	}
	return 0;
}

/* Parse-time entry point for @DSIDX@ blocks. Three regimes:
 *   1. Explicit DSCOUNT N -> expand defs[] in place; final shape is fixed.
 *   2. No DSCOUNT, but defs use @DSIDX@ -> leave defs[] as templates and
 *      mark dsidx_runtime=1, so render time can pick N from the actual
 *      RRD file (different hosts may store different sample counts).
 *   3. No @DSIDX@ at all -> nothing to do.
 */
static void expand_dsidx_in_block(gdef_t *gd)
{
	if (gd->defs == NULL) return;

	if (gd->dscount > 0) {
		char **expanded = expand_dsidx_array(gd->defs, gd->dscount);
		int i;
		for (i = 0; gd->defs[i]; i++) xfree(gd->defs[i]);
		xfree(gd->defs);
		gd->defs = expanded;
		return;
	}

	if (defs_use_dsidx(gd->defs)) gd->dsidx_runtime = 1;
}

/* Pull the .rrd filename out of a DEF line of the form
 *   DEF:var=FILENAME:ds:CF
 * Returns a freshly-allocated string (caller frees) or NULL if the line
 * isn't a DEF or doesn't fit that shape. */
static char *def_rrdfile(const char *defline)
{
	const char *eq, *colon;
	char *out;
	size_t n;

	if (strncmp(defline, "DEF:", 4) != 0) return NULL;
	eq = strchr(defline + 4, '=');
	if (!eq) return NULL;
	colon = strchr(eq + 1, ':');
	if (!colon || colon == eq + 1) return NULL;
	n = colon - (eq + 1);
	out = (char *)malloc(n + 1);
	memcpy(out, eq + 1, n);
	out[n] = '\0';
	return out;
}

/* Walk a DS-template like "ping@DSIDX@" and return the literal prefix
 * before @DSIDX@ ("ping"). Caller frees. NULL if no @DSIDX@ present. */
static char *dsname_prefix(const char *tmpl)
{
	const char *at = strstr(tmpl, "@DSIDX@");
	char *out;
	size_t n;
	if (!at) return NULL;
	n = at - tmpl;
	out = (char *)malloc(n + 1);
	memcpy(out, tmpl, n);
	out[n] = '\0';
	return out;
}

/* Pull the DS name template out of a DEF line:
 *   DEF:var=file.rrd:DSNAME:CF -> DSNAME
 * Caller frees; NULL on parse failure. */
static char *def_dsname(const char *defline)
{
	const char *eq, *c1, *c2;
	char *out;
	size_t n;

	if (strncmp(defline, "DEF:", 4) != 0) return NULL;
	eq = strchr(defline + 4, '=');
	if (!eq) return NULL;
	c1 = strchr(eq + 1, ':');
	if (!c1) return NULL;
	c2 = strchr(c1 + 1, ':');
	if (!c2 || c2 == c1 + 1) return NULL;
	n = c2 - (c1 + 1);
	out = (char *)malloc(n + 1);
	memcpy(out, c1 + 1, n);
	out[n] = '\0';
	return out;
}
