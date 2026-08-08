#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/packaging/loadhosts-aliasing.sh
#
# xmh_item(), xmh_item_walk(), xmh_item_byname() and xmh_item_multi() return a
# pointer INTO the host record, not a copy: a caller that writes through it
# corrupts the stored configuration for every later reader. That caused #276
# and #300; the fix is always the same line -- copy before tokenising. This
# test hunts the pattern, so the class cannot regrow quietly.
#
# One awk pass per file: comments and string literals are stripped, brace
# depth is tracked (first #if branch taken), and every assignment from the
# API -- any shape, spacing, cast, chain or ternary -- opens a scan segment
# running to the end of its function. Inside a segment, a standard-library
# call writing through the variable is reported (strsep via its &v form, a
# mutator handed the API result directly too). A reassignment ends the
# segment -- comparisons and refetches cannot mask it -- except a refetch
# from the API, which keeps the alias. A fixture self-test runs first; its
# expected report is generated from the fixture's "HIT: <mutator> <var>"
# markers, so a scanner regression fails with a one-line diff.
#
# The mutator list is standard-library only: a project function that mutates
# its argument is a bug in that function (#298, fixed by #299), not an entry
# here.
#
# KNOWN BLIND SPOTS, on purpose: pointers derived from the alias (#278's
# strchr shape); struct-member targets; assignments split across lines;
# aliases handed back through out-parameters (svcstatus.c's loadhostdata()).
# The complete fix is const-qualifying the accessors -- 194 assignments in
# 30 files; this narrows the class for a fraction of that.
#
# Source-only: no build required.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/lib/loadhosts.c" ] || skip "lib/loadhosts.c not present in this checkout"
command -v awk >/dev/null 2>&1 || skip "awk not available"

work=$(mktempdir)

# Write through their first argument; read-only friends are absent
# deliberately, and strsep (whose only shape passes &v) is matched separately.
MUTATORS='strtok strtok_r strcpy strcat strncpy strncat stpcpy sprintf snprintf memset memcpy memmove strxfrm'

# xmh_item_id() is absent on purpose: it points into static name tables, not
# the host record.
SCANNER=$(cat <<'AWK'
BEGIN {
	n = split(muts, m, " ")
	nseg = 0
	inc = 0        # inside a /* */ comment
	depth = 0      # brace depth
	ppdepth = 0    # #if nesting
	ppskip = 0     # #if level whose #else/#elif branch we are skipping
}

# Remove comments and string/char literals; cross-line /* */ state lives in
# `inc`, literals become empty ones so surrounding tokens stay separated.
function strip(s,   out, i, len, c, d, q) {
	out = ""; i = 1; len = length(s)
	while (i <= len) {
		c = substr(s, i, 1); d = substr(s, i, 2)
		if (inc) { if (d == "*/") { inc = 0; i += 2 } else i++; continue }
		if (d == "/*") { inc = 1; i += 2; continue }
		if (d == "//") break
		if (c == "\"" || c == "'") {
			q = c; i++
			while (i <= len) {
				c = substr(s, i, 1); i++
				if (c == "\\") { i++; continue }
				if (c == q) break
			}
			out = out q q
			continue
		}
		out = out c; i++
	}
	return out
}

{
	# Preprocessor lines (outside comments): take the first branch of a
	# conditional, so #else alternatives cannot double-count braces.
	if (!inc && $0 ~ /^[ \t]*#/) {
		if ($0 ~ /^[ \t]*#[ \t]*if/) ppdepth++
		else if ($0 ~ /^[ \t]*#[ \t]*(else|elif)/) { if (!ppskip && ppdepth) ppskip = ppdepth }
		else if ($0 ~ /^[ \t]*#[ \t]*endif/) { if (ppskip == ppdepth) ppskip = 0; if (ppdepth) ppdepth-- }
		strip($0)                      # a trailing /* still opens a comment
		next
	}
	line = strip($0)                       # keeps comment state everywhere
	if (ppskip) next                       # non-taken conditional branch
	probe = " " line                       # guard char for column-0 matches

	# Nothing below can match a line without the substring; skip the
	# expensive dynamic regexes for the common case.
	if (index(probe, "xmh_item")) {

	# A mutator handed the API result directly is an offence on its own.
	for (i = 1; i <= n; i++)
		if (index(probe, m[i]) && match(probe, "[^A-Za-z0-9_]" m[i] "[ \t]*\\([ \t]*(\\([ \tA-Za-z_*]*\\)[ \t]*)?xmh_item(_walk|_byname|_multi)?[ \t]*\\("))
			printf "  %s:%d  %s() writes through \"xmh_item()\", which aliases the host record\n", f, NR, m[i]

	# Each assignment from the API opens a segment just past its own call --
	# plain, chained (a = b = ...) or ternary, comparisons in the condition
	# included. The guard char rejects "==" and struct-member targets.
	pos = 1
	while (match(substr(probe, pos), /[^A-Za-z0-9_=>.][A-Za-z_][A-Za-z0-9_]*[ \t]*=([ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*=)*(([^;{}=]|[=!<>]=)*[?:])?[ \t]*(\([ \tA-Za-z_*]*\)[ \t]*)?xmh_item(_walk|_byname|_multi)?\(/)) {
		mend = pos + RSTART - 1 + RLENGTH
		pre = substr(probe, pos + RSTART - 1, RLENGTH)
		sub(/(\([ \tA-Za-z_*]*\)[ \t]*)?xmh_item(_walk|_byname|_multi)?\($/, "", pre)
		sub(/[?:].*$/, "", pre)            # drop a ternary condition
		sub(/^./, "", pre)                 # drop the guard char
		# assignment targets: identifiers followed by "=", but not "=="
		while (match(pre, /[A-Za-z_][A-Za-z0-9_]*[ \t]*=/)) {
			vtxt = substr(pre, RSTART, RLENGTH)
			nxt = substr(pre, RSTART + RLENGTH, 1)
			pre = substr(pre, RSTART + RLENGTH)
			if (nxt == "=") { sub(/^=+/, "", pre); continue }
			sub(/[ \t]*=$/, "", vtxt)
			nseg++
			vars[nseg] = vtxt
			startNR[nseg] = NR
			startpos[nseg] = mend
			active[nseg] = 1
		}
		pos = mend
	}

	}

	for (j = 1; j <= nseg; j++) {
		if (!active[j]) continue
		v = vars[j]
		seg = (NR == startNR[j]) ? " " substr(probe, startpos[j]) : probe
		if (!index(seg, v)) continue       # nothing here can involve v

		# A reassignment ends the segment; scan past "==" comparisons and
		# refetches from the API (which keep the alias) to find one.
		rpos = 0
		off = 1
		while (match(substr(seg, off), "[^A-Za-z0-9_]" v "[ \t]*=[ \t]*")) {
			p0 = off + RSTART - 1
			rhs = substr(seg, p0 + RLENGTH)
			off = p0 + RLENGTH
			if (rhs ~ /^=/) { off++; continue }
			if (rhs ~ /^(\([ \tA-Za-z_*]*\)[ \t]*)?xmh_item(_walk|_byname|_multi)?[ \t]*\(/) continue
			rpos = p0
			break
		}

		# Mutations count only before the reassignment. Bare v only:
		# writing through &v hits the local pointer, not the record --
		# except strsep, whose &v form writes through *v as well.
		for (i = 1; i <= n; i++)
			if (index(seg, m[i]) && match(seg, "[^A-Za-z0-9_]" m[i] "[ \t]*\\([ \t]*(\\([ \tA-Za-z_*]*\\)[ \t]*)?" v "[ \t]*[,)]"))
				if (!rpos || RSTART < rpos)
					printf "  %s:%d  %s() writes through \"%s\", which aliases the host record\n", f, NR, m[i], v
		if (index(seg, "strsep") && match(seg, "[^A-Za-z0-9_]strsep[ \t]*\\([ \t]*&[ \t]*" v "[ \t]*[,)]"))
			if (!rpos || RSTART < rpos)
				printf "  %s:%d  strsep() writes through \"%s\", which aliases the host record\n", f, NR, v

		if (rpos) active[j] = 0
	}

	# Leaving the function closes its segments, wherever the brace sits.
	depth += gsub(/\{/, "{", line) - gsub(/\}/, "}", line)
	if (depth <= 0) {
		depth = 0
		for (j = 1; j <= nseg; j++) active[j] = 0
	}
}
AWK
)

# scan_file FILE DISPLAYNAME -- one line per mutation of an aliased pointer;
# a file with no call sites produces no output.
scan_file() {
	awk -v muts="$MUTATORS" -v f="$2" "$SCANNER" "$1"
}

# --- Self-test ---------------------------------------------------------------
# Every shape the scanner must catch, and correct code it must leave alone.
cat > "$work/selftest.c" <<'EOF'
static void decl_form(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	strtok(tag, ",");	/* HIT: strtok tag */
}

static void assign_form(void *hinfo)
{
	char *tag;
	tag = xmh_item(hinfo, XMH_NOFLAP);
	strtok_r(tag, ",", &sp);	/* HIT: strtok_r tag */
}

static void condition_form(void *hinfo)
{
	char *tag;
	if ((tag = xmh_item(hinfo, XMH_NOFLAP)) != NULL) strcpy(tag, "x");	/* HIT: strcpy tag */
}

static void multi_form(void *hinfo)
{
	char *page = xmh_item_multi(hinfo, XMH_PAGEPATH);
	strtok(page, "/");	/* HIT: strtok page */
}

static void tab_form(void *hinfo)
{
	char *tag =	xmh_item(hinfo, XMH_NOFLAP);
	strtok(tag, ",");	/* HIT: strtok tag */
}

static void spaced_mutator(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	strtok ( tag, "," );	/* HIT: strtok tag */
}

static void two_on_one_line(void *hinfo)
{
	char *a, *b;
	a = xmh_item(hinfo, XMH_NOFLAP); b = xmh_item(hinfo, XMH_COMPACT);
	strtok(a, ",");	/* HIT: strtok a */
}

static void cast_form(void *hinfo)
{
	char *tag = (char *)xmh_item(hinfo, XMH_NOFLAP);
	strtok(tag, ",");	/* HIT: strtok tag */
}

static void direct_form(void *hinfo)
{
	strtok(xmh_item(hinfo, XMH_NOFLAP), ",");	/* HIT: strtok xmh_item() */
}

static void sprintf_form(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	sprintf(tag, "%d", 1);	/* HIT: sprintf tag */
}

static void ternary_form(void *hinfo)
{
	char *tag;
	tag = always ? xmh_item(hinfo, XMH_NOFLAP) : NULL;
	strtok(tag, ",");	/* HIT: strtok tag */
}

static void chained_form(void *hinfo)
{
	char *a, *b;
	a = b = xmh_item(hinfo, XMH_NOFLAP);
	strtok(a, ",");	/* HIT: strtok a */
}

static void ternary_cmp_form(void *hinfo)
{
	char *tag;
	tag = (n == 2) ? xmh_item(hinfo, XMH_NOFLAP) : NULL;
	strtok(tag, ",");	/* HIT: strtok tag */
}

static void strsep_form(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	strsep(&tag, ",");	/* HIT: strsep tag */
}

static void cast_argument(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	strtok((char *)tag, ",");	/* HIT: strtok tag */
}

static void comment_hash(void *hinfo)
{
	/*
#ifdef OLD
#endif */
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	strtok(tag, ",");	/* HIT: strtok tag */
}

static void walk_form(void *hinfo)
{
	char *item;

	for (item = xmh_item_walk(hinfo); (item); item = xmh_item_walk(NULL))
		strtok(item, ",");	/* HIT: strtok item */
}

static void mutate_then_copy(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	strtok(tag, ","); tag = strdup(tag);	/* HIT: strtok tag */
}

static void copy_first(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	if (tag) tag = strdup(tag);
	strtok(tag, ",");
}

static void reassign_other(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	tag = scratchbuf;
	strtok(tag, ",");
}

static void reassign_after_compare(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	int missing = (tag == NULL); tag = strdup(deftag);
	strtok(tag, ",");
}

static void comment_mention(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	/* a correct caller never hands strtok(tag, ...) the raw pointer */
	use(tag);
}

static void mutate_before_refetch(void *hinfo)
{
	char *tag = strdup(deftag);
	strtok(tag, ","); tag = xmh_item(hinfo, XMH_NOFLAP);
}

static void member_form(void *hinfo)
{
	/* blind spot, documented: struct-member targets are skipped */
	alert->location = xmh_item(hinfo, XMH_ALLPAGEPATHS);
	strtok(alert->location, ",");
}

static void address_of_local(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	memset(&tag, 0, sizeof(tag));
}

static void refetch_lookalike(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	tag = xmh_item_name(idx);
	strtok(tag, ",");
}

#ifdef ALT
static void pp_form(void *hinfo)
{
#else
static void pp_form(void *hinfo)
{
#endif
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	if (tag) return;
}

static void indented_close(void *hinfo)
{
	char *tag = xmh_item(hinfo, XMH_NOFLAP);
	if (!tag) return;
	}

static void takes_own_buffer(char *tag)
{
	strtok(tag, ",");
}
EOF

{ grep -n 'HIT:' "$work/selftest.c" || true; } \
	| sed -n 's|^\([0-9]*\):.*HIT: \([A-Za-z0-9_]*\) \([A-Za-z0-9_()]*\).*|  selftest.c:\1  \2() writes through "\3", which aliases the host record|p' \
	| sort -u > "$work/selftest.expected"
[ -s "$work/selftest.expected" ] || fail "self-test fixture has no HIT markers -- the marker syntax drifted"

scan_file "$work/selftest.c" selftest.c | sort -u > "$work/selftest.out"
diff -u "$work/selftest.expected" "$work/selftest.out" >&2 \
	|| fail "self-test: scanner output differs from expected (diff above)"

# --- The real scan -----------------------------------------------------------
# Tracked sources when this tree is the tracked one; otherwise (a tarball,
# even one unpacked inside some other git repo) everything present.
cd "$ROOT"
if git ls-files --error-unmatch lib/loadhosts.c >/dev/null 2>&1; then
	git ls-files -- '*.c' | { xargs -r grep -l 'xmh_item' || true; } > "$work/files"
else
	grep -rl 'xmh_item' --include=*.c . > "$work/files" || true
fi
[ -s "$work/files" ] || fail "found no xmh_item() call sites -- the parser is stale"

while IFS= read -r file; do
	scan_file "$file" "${file#./}"
done < "$work/files" > "$work/offenders"

sort -u -o "$work/offenders" "$work/offenders"

[ ! -s "$work/offenders" ] || fail "a pointer from the loadhosts API is passed to a function that writes \
through it. xmh_item() and friends return a pointer into the host record's own tag buffer, so this corrupts \
the stored configuration for every later reader (see #276, #298, #300). Copy it first -- \
xymond/xymond_client.c's want_msgtype() shows the pattern:
$(cat "$work/offenders")"

pass "no caller mutates a pointer returned by the loadhosts API"
