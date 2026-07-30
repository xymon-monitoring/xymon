#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/packaging/hosts-cfg-tag-case.sh
#
# hosts.cfg(5) must spell each tag the same way lib/loadhosts.c stores it in
# xmh_item_key[]. The case matters: xmh_find_item() matches a tag
# case-insensitively (so either spelling sets the attribute), but
# xmh_item_idx() -- which web/svcstatus-info.c uses to decide a tag is
# already a recognized attribute, and xymonnet/xymonnet.c to decide a tag is
# not a network test -- matches case-sensitively. A tag written in the
# documented case therefore has to match the stored key exactly, or following
# the manual produces a tag that works yet is reported as unrecognized.
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

pass "hosts.cfg(5) documents every tag in the case lib/loadhosts.c stores it"
