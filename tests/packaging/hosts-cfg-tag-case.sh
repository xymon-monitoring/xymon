#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/packaging/hosts-cfg-tag-case.sh
#
# A tag written the way hosts.cfg(5) documents it must be recognized by
# xmh_item_idx(). Today that function compares case-sensitively, so the
# documented spelling has to match the stored xmh_item_key[] entry exactly.
# If xmh_item_idx() becomes case-insensitive, the manual's tag case is free and
# this test must not preserve the old case policy as an accidental invariant.
#
# Four tags used to be documented lower-case while stored upper-case
# (noclear, pulldata, noflap, multihomed); they were the only tags where
# reading the manual triggered that mismatch.
#
# Compares every ".IP <tag>" entry in the manpage against the key table, so
# a new tag documented in the wrong case is caught too. Tags with no manpage
# entry are not flagged here -- several are genuinely undocumented, which is
# a separate gap.
#
# No build required: both inputs are source files.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
LOADHOSTS="$ROOT/lib/loadhosts.c"
MANPAGE="$ROOT/common/hosts.cfg.5"

for f in "$LOADHOSTS" "$MANPAGE"; do
	[ -f "$f" ] || skip "$(basename "$f") not present in this checkout"
done

command -v awk >/dev/null 2>&1 || skip "awk not available"

work=$(mktempdir)

# Mirror the comparison xmh_item_idx() actually performs instead of fixing a
# case policy that the implementation may deliberately change.
idx_body=$(awk '/^int xmh_item_idx/,/^}/' "$LOADHOSTS")
[ -n "$idx_body" ] || fail "could not locate xmh_item_idx() in lib/loadhosts.c"
case "$idx_body" in
	*strncasecmp*) pass "xmh_item_idx() matches case-insensitively, so the manual's tag case is free" ;;
	*strncmp*) ;;
	*) fail "xmh_item_idx() no longer compares keys with strncmp()/strncasecmp() -- this test needs updating" ;;
esac

# Stored keys, minus the ':' / '=' value separator: "COMMENT:" -> COMMENT
sed -nE 's/.*xmh_item_key\[XMH_[A-Z0-9_]+\][[:space:]]*=[[:space:]]*"([^"]*)".*/\1/p' \
	"$LOADHOSTS" | sed -E 's/[:=]$//' | sort -u > "$work/keys"
[ -s "$work/keys" ] || fail "found no xmh_item_key[] entries in lib/loadhosts.c -- parser is stale"

# Documented tag names: leading identifier of each ".IP" entry, unquoted.
sed -nE 's/^\.IP[[:space:]]+"?([A-Za-z0-9_-]+).*/\1/p' "$MANPAGE" | sort -u > "$work/documented"
[ -s "$work/documented" ] || fail "found no .IP entries in hosts.cfg.5 -- parser is stale"

# A documented tag whose lower-cased form matches a key's lower-cased form,
# but which differs from the key in case, is a mismatch.
mismatched=$(awk '
	NR == FNR { key[tolower($0)] = $0; next }
	{ lc = tolower($0); if (lc in key && key[lc] != $0) printf "  hosts.cfg(5) says \"%s\", lib/loadhosts.c stores \"%s\"\n", $0, key[lc] }
' "$work/keys" "$work/documented")

[ -z "$mismatched" ] || fail "hosts.cfg(5) documents tags in a different case than lib/loadhosts.c stores them.
Because xmh_item_idx() matches case-sensitively, the documented spelling must match the stored key exactly:
$mismatched"

pass "hosts.cfg(5) documents every tag in the case-sensitive form xmh_item_idx() recognizes"
