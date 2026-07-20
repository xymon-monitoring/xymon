/*
 * web/aggregate-tokens.inc.c
 *
 * Shared parser + RPN-emitter for graphs.cfg aggregate template tokens.
 * Two families:
 *
 *   "across files" -- iterate over the RRDs matched by FNPATTERN:
 *     @AVG: @SUM: @MIN: @MAX: @AVGNAN: @SUMNAN: @MINNAN: @MAXNAN:
 *     @COUNT: @MEDIAN: @STDEV: @PERCENT: @P<pct>:
 *
 *   "within one file" -- iterate over a 1-indexed DS prefix run inside
 *   one matched RRD (used by smokeping's [conn-smoke] block, where the
 *   N ping samples live as N DSes in one tcp.conn-smoke.rrd):
 *     @DSMEDIAN:
 *
 * The within-file family reads N from the file-static `aggregate_dscount`.
 * Callers set it before invoking expand_aggregate_tokens on a runtime-
 * expanded block, then reset it to 0 afterwards. A zero value emits UNKN.
 *
 * This file is **#include**'d by both web/showgraph.c (production) and
 * web/test-aggregate-tokens.c (golden-output test). Including it from
 * two TUs keeps the parser and the test in lockstep: any future bug fix
 * here is visible to both without manual mirroring.
 *
 * The including TU must provide:
 *   - file-scope: int rrddbcount, int firstidx, int lastidx
 *   - strbuffer_t and the addtobuffer / addtobufferraw functions
 *
 * All symbols below are `static` so multiple includes (or different
 * including TUs in the same program, which isn't a current use case)
 * stay isolated.
 */

/* DS count for the current within-file aggregate. Set by callers (e.g.
 * smoke's render-time loop) immediately before invoking
 * expand_aggregate_tokens on a block that may use @DSMEDIAN: et al.
 * Zero means "no DS count available"; the within-file aggregate emits
 * UNKN. Lives at file scope (not a parameter) so the existing
 * expand_aggregate_tokens / add_aggregate_rpn signatures stay
 * untouched and so the test can drive it the same way. */
static int aggregate_dscount = 0;

static int selected_rrdidx(int idx)
{
	return ((firstidx == -1) || ((idx >= firstidx) && (idx <= lastidx)));
}

static int is_aggregate_token(char *inp, char **op, char **name, int *oplen, int *toklen)
{
	char *p;

	if (strncmp(inp, "@AVG:", 5) == 0) {
		*op = "AVG";
		*oplen = 5;
	}
	else if (strncmp(inp, "@SUM:", 5) == 0) {
		*op = "SUM";
		*oplen = 5;
	}
	else if (strncmp(inp, "@MIN:", 5) == 0) {
		*op = "MIN";
		*oplen = 5;
	}
	else if (strncmp(inp, "@MAX:", 5) == 0) {
		*op = "MAX";
		*oplen = 5;
	}
	else if (strncmp(inp, "@AVGNAN:", 8) == 0) {
		*op = "AVGNAN";
		*oplen = 8;
	}
	else if (strncmp(inp, "@SUMNAN:", 8) == 0) {
		*op = "SUMNAN";
		*oplen = 8;
	}
	else if (strncmp(inp, "@MINNAN:", 8) == 0) {
		*op = "MINNAN";
		*oplen = 8;
	}
	else if (strncmp(inp, "@MAXNAN:", 8) == 0) {
		*op = "MAXNAN";
		*oplen = 8;
	}
	else if (strncmp(inp, "@COUNT:", 7) == 0) {
		*op = "COUNT";
		*oplen = 7;
	}
	else if (strncmp(inp, "@MEDIAN:", 8) == 0) {
		*op = "MEDIAN";
		*oplen = 8;
	}
	else if (strncmp(inp, "@STDEV:", 7) == 0) {
		*op = "STDEV";
		*oplen = 7;
	}
	else if (strncmp(inp, "@PERCENT:", 9) == 0) {
		*op = "PERCENT";
		*oplen = 9;
	}
	else if (strncmp(inp, "@DSMEDIAN:", 10) == 0) {
		/* Within-file aggregate: iterates name1..nameN where N is
		 * the file-scope `aggregate_dscount`. The caller sets that
		 * before invoking the parser; zero -> UNKN. */
		*op = "DSMEDIAN";
		*oplen = 10;
	}
	else if ((inp[0] == '@') && (inp[1] == 'P') && isdigit((int)inp[2])) {
		/* @P<pct>: shorthand for @PERCENT:name:pct@. Accept a decimal
		 * number with at most one dot (95, 99.9, 0.5); reject 95.5.5,
		 * ., 9. -- the earlier any-number-of-dots parse produced RPN
		 * that rrdtool refused. The `D...' prefix space is reserved
		 * for the smokeping branch's @DSIDX@ family; don't add ops
		 * starting with D here. */
		int dots = 0;
		p = inp + 3;
		while (1) {
			if (isdigit((int)*p)) { p++; continue; }
			if (*p == '.' && dots == 0) { dots = 1; p++; continue; }
			break;
		}
		if (*p != ':') return 0;

		/* For @P<pct>:name@, *op points into the input at the digits
		 * (e.g. "95:name@..."), NOT to a NUL-terminated operator
		 * literal. Callers detect this form via isdigit(op[0]) and
		 * read pct via the leading digit run; never strcmp it against
		 * an operator name. */
		*op = inp + 2;
		*oplen = (p - inp) + 1;
	}
	else {
		return 0;
	}

	*name = inp + *oplen;
	p = strchr(*name, '@');
	if (p == NULL) return 0;
	/* Reject empty operand: @AVG:@ used to "succeed" with namelen=0,
	 * producing RPN that references undefined DEF vars (0,1,2,3,AVG)
	 * which rrdtool rejects with a confusing message. */
	if (p == *name) return 0;
	/* Reject names that contain whitespace, comma, or semicolon. Without
	 * this, "@AVG:foo bar @WHATEVER@" would swallow the @WHATEVER@
	 * token's leading @ as part of the unbounded strchr search,
	 * silently corrupting the surrounding template. Stay permissive on
	 * the rest -- "name" may carry "." (for @PERCENT:n:99.9@), ":" (the
	 * inner separator), and identifier characters used in DEF names. */
	{
		char *q;
		for (q = *name; q < p; q++) {
			if (isspace((int)*q) || *q == ',' || *q == ';') return 0;
		}
	}
	*toklen = (p - inp) + 1;

	return 1;
}

static int def_uses_aggregate(char *def)
{
	char *p;

	for (p = strchr(def, '@'); p; p = strchr(p + 1, '@')) {
		char *op, *name;
		int oplen, toklen;

		if (is_aggregate_token(p, &op, &name, &oplen, &toklen)) return 1;
	}

	return 0;
}

static void add_aggregate_var(strbuffer_t *result, char *name, int namelen, int idx)
{
	char numstr[20];

	addtobufferraw(result, name, namelen);
	snprintf(numstr, sizeof(numstr), "%d", idx);
	addtobuffer(result, numstr);
}

static void add_aggregate_varlist(strbuffer_t *result, char *name, int namelen, int *count)
{
	int i;

	*count = 0;
	for (i = 0; (i < rrddbcount); i++) {
		if (!selected_rrdidx(i)) continue;

		if (*count > 0) addtobuffer(result, ",");
		add_aggregate_var(result, name, namelen, i);
		(*count)++;
	}
}

static void add_aggregate_count_rpn(strbuffer_t *result, char *name, int namelen)
{
	int i, n = 0;

	for (i = 0; (i < rrddbcount); i++) {
		if (!selected_rrdidx(i)) continue;

		if (n > 0) addtobuffer(result, ",");
		add_aggregate_var(result, name, namelen, i);
		addtobuffer(result, ",UN,0,1,IF");
		if (n > 0) addtobuffer(result, ",+");
		n++;
	}

	/* Empty selection: emit "0" rather than UNKN so callers can divide
	 * (UNKN/0 = UNKN in rrdtool; division by literal 0 yields UNKN as
	 * well, so dividing by @COUNT: stays safe in either case). Other
	 * ops emit "UNKN" for the empty case; the inconsistency is
	 * intentional: count-of-nothing is 0, sum/avg-of-nothing is
	 * undefined. */
	if (n == 0) addtobuffer(result, "0");
}

static void add_aggregate_rpn(strbuffer_t *result, char *op, char *name, int namelen)
{
	int i, n = 0;
	char *pct = NULL;
	int pctlen = 0;
	int ispercent = ((strcmp(op, "PERCENT") == 0) || isdigit((int)op[0]));

	if (ispercent) {
		char *p;

		if (isdigit((int)op[0])) {
			/* @P<pct>:name@ -- pct already validated in is_aggregate_token. */
			pct = op;
			p = op;
			while (isdigit((int)*p) || (*p == '.')) p++;
			pctlen = (p - op);
		}
		else if (namelen > 0) {
			/* @PERCENT:name:pct@ -- pct lives after an inner colon.
			 * Validate the same digits+one-dot shape that is_aggregate_token
			 * enforces for @P<pct>:; otherwise @PERCENT:t:abc@ and
			 * @PERCENT:t:95:garbage@ would emit garbage into the RPN. */
			p = memchr(name, ':', namelen);
			if (p) {
				int i, dots = 0, ok = 1;
				pct = p + 1;
				pctlen = namelen - (p - name) - 1;
				namelen = (p - name);
				for (i = 0; ok && i < pctlen; i++) {
					if (isdigit((int)pct[i])) continue;
					if (pct[i] == '.' && dots == 0) { dots = 1; continue; }
					ok = 0;
				}
				if (!ok) pctlen = 0;
			}
		}

		if ((pct == NULL) || (pctlen == 0) || (namelen == 0)) {
			addtobuffer(result, "UNKN");
			return;
		}
	}

	if (strcmp(op, "COUNT") == 0) {
		add_aggregate_count_rpn(result, name, namelen);
		return;
	}

	/* Within-file aggregate: walks 1..aggregate_dscount instead of the
	 * 0..rrddbcount-1 used by the across-files family. The 1-indexing
	 * matches @DSIDX@'s convention so a [conn-smoke]-style block with
	 * DEFs named ping1..pingN can use @DSMEDIAN:ping@ directly. */
	if (strcmp(op, "DSMEDIAN") == 0) {
		int i;
		char numstr[20];

		if (aggregate_dscount <= 0) {
			addtobuffer(result, "UNKN");
			return;
		}
		for (i = 1; i <= aggregate_dscount; i++) {
			if (i > 1) addtobuffer(result, ",");
			addtobufferraw(result, name, namelen);
			snprintf(numstr, sizeof(numstr), "%d", i);
			addtobuffer(result, numstr);
		}
		snprintf(numstr, sizeof(numstr), ",%d,MEDIAN", aggregate_dscount);
		addtobuffer(result, numstr);
		return;
	}

	if ((strcmp(op, "AVGNAN") == 0) || (strcmp(op, "MEDIAN") == 0) ||
	    (strcmp(op, "STDEV") == 0) || ispercent) {
		add_aggregate_varlist(result, name, namelen, &n);

		if (n == 0) {
			addtobuffer(result, "UNKN");
			return;
		}

		if (ispercent) {
			char numstr[20];

			addtobuffer(result, ",");
			addtobufferraw(result, pct, pctlen);
			snprintf(numstr, sizeof(numstr), ",%d,PERCENT", n);
			addtobuffer(result, numstr);
		}
		else {
			char numstr[20];

			snprintf(numstr, sizeof(numstr), ",%d,%s", n, ((strcmp(op, "AVGNAN") == 0) ? "AVG" : op));
			addtobuffer(result, numstr);
		}
		return;
	}

	for (i = 0; (i < rrddbcount); i++) {
		if (!selected_rrdidx(i)) continue;

		if (n == 0) {
			add_aggregate_var(result, name, namelen, i);
		}
		else {
			addtobuffer(result, ",");
			add_aggregate_var(result, name, namelen, i);
			if (strcmp(op, "MIN") == 0) addtobuffer(result, ",MIN");
			else if (strcmp(op, "MAX") == 0) addtobuffer(result, ",MAX");
			else if (strcmp(op, "MINNAN") == 0) addtobuffer(result, ",MINNAN");
			else if (strcmp(op, "MAXNAN") == 0) addtobuffer(result, ",MAXNAN");
			else if (strcmp(op, "SUMNAN") == 0) addtobuffer(result, ",ADDNAN");
			else addtobuffer(result, ",+");
		}
		n++;
	}

	if (n == 0) {
		addtobuffer(result, "UNKN");
	}
	else if (strcmp(op, "AVG") == 0) {
		/* Divides by the SELECTED count, not the non-NaN count.
		 * If any selected DEF is UNKN, the running sum propagates
		 * UNKN and the result is UNKN. For NaN-aware averaging,
		 * combine @SUMNAN:/@COUNT: at the caller. */
		char numstr[20];

		snprintf(numstr, sizeof(numstr), ",%d,/", n);
		addtobuffer(result, numstr);
	}
}
