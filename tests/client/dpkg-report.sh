#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/dpkg-report.sh
#
# Behavioural guard for the reformatted dpkg output added to
# xymonclient-linux.sh by xymon-monitoring/xymon#48: the client sends a compact
# three-column `dpkg -l` listing (status, name, version) under a `[dpkg]` tag,
# instead of the full wide table.
#
# We run the script's actual reformat pipeline with a `dpkg` stub on PATH that
# emits a canned `dpkg -l` table, and assert the output keeps status/name/
# version and drops the architecture/description columns and the header. Like
# fs-filter.sh, this extracts the pipeline from the script, so a restructuring
# that breaks that line stops catching the logic -- a conscious trade. Skips
# only when the environment is absent (no client script, or no awk); if a
# present client script has lost the dpkg reformat pipeline, that is a
# regression and the test fails rather than skipping green.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
# Default: the in-tree script. $XYMONCLIENT_LINUX (autopkgtest) points at the
# INSTALLED script -- /usr/lib/xymon/client/bin/xymonclient-linux.sh on Debian
# -- so the test exercises what the package actually ships, distro patches
# included. Same-version artifacts only; see "Path discovery" in
# tests/README.md.
SCRIPT="${XYMONCLIENT_LINUX:-$ROOT/client/xymonclient-linux.sh}"

[ -f "$SCRIPT" ] || skip "$SCRIPT absent"
command -v awk >/dev/null 2>&1 || skip "no awk"

# The client script ships in the same tree as this test, so a missing dpkg
# reformat pipeline means the #48 behaviour was removed -- a regression, not a
# pre-feature tree -- and must fail. (A genuinely absent environment -- no awk,
# or no client script at all -- already skipped above.)
pipeline=$(grep -E 'dpkg -l[[:space:]]*\|[[:space:]]*awk' "$SCRIPT" | head -1 || true)
[ -n "$pipeline" ] || fail "xymonclient-linux.sh lost the dpkg reformat pipeline (#48)"

# The reformatted listing is only useful under the [dpkg] section tag the
# server parses. Extracting the pipeline alone never checks the tag, so a
# rename (or a dropped header) would slip through as a false green. Assert the
# [dpkg] tag is emitted and that it precedes the pipeline that fills it.
# `|| true`: a no-match grep exits 1, which under `set -e -o pipefail` would
# abort the script before the explicit fail() below could report the reason.
tag_line=$( (grep -n '^[[:space:]]*echo "\[dpkg\]"' "$SCRIPT" || true) | head -1 | cut -d: -f1)
pipe_line=$( (grep -nE 'dpkg -l[[:space:]]*\|[[:space:]]*awk' "$SCRIPT" || true) | head -1 | cut -d: -f1)
[ -n "$tag_line" ] || fail "xymonclient-linux.sh no longer emits the [dpkg] section tag (#48)"
[ "$tag_line" -lt "$pipe_line" ] \
	|| fail "the [dpkg] section tag must precede the dpkg reformat pipeline (#48)"

# Behavioural extraction below runs the pipeline in isolation, so the real
# script's enclosing guard is never executed -- flipping it to `if false` would
# emit no [dpkg] report yet leave this test green. Statically assert the guard
# still gates the block on dpkg actually being present, so a falsified condition
# (a constant, or a removed `command -v dpkg`) fails here.
grep -Eq 'if[[:space:]]+command -v dpkg[[:space:]]' "$SCRIPT" \
	|| fail "xymonclient-linux.sh no longer gates the [dpkg] report on dpkg presence (#48)"

work=$(mktempdir)
mkdir "$work/bin"
cat >"$work/bin/dpkg" <<'EOF'
#!/bin/sh
[ "$1" = "-l" ] || exit 0
cat <<'TBL'
Desired=Unknown/Install/Remove/Purge/Hold
| Status=Not/Inst/Conf-files/Unpacked/halF-conf/Half-inst/trig-aWait/Trig-pend
|/ Err?=(none)/Reinst-required (Status,Err: uppercase=bad)
||/ Name           Version      Architecture Description
+++-==============-============-============-=================================
ii  bash           5.1-6        amd64        GNU Bourne Again SHell
ii  coreutils      8.32-4       amd64        GNU core utilities
rc  oldpkg         1.0          amd64        removed, config remains
TBL
EOF
chmod +x "$work/bin/dpkg"

out=$(PATH="$work/bin:$PATH" bash -c "$pipeline")

# reformatted three-column rows kept
assert_contains "ii bash 5.1-6" "$out" "dpkg report lost the compact status/name/version row (#48)"
assert_contains "ii coreutils 8.32-4" "$out" "dpkg report dropped an installed package (#48)"
# non-installed rows (rc = removed, config remains) must survive too: keeping
# the status column is the whole point of #48, so a pipeline that narrowed to
# `ii` only -- dropping rc/hold/half-installed states -- is a regression.
assert_contains "rc oldpkg 1.0" "$out" "dpkg report dropped the non-installed (rc) row -- the status column is the point of #48"
# the wide-table noise dropped
assert_not_contains "GNU Bourne Again SHell" "$out" "dpkg report still carries the description column (#48)"
assert_not_contains "Architecture" "$out" "dpkg report still carries the table header (#48)"

pass "xymonclient-linux.sh emits the compact #48 dpkg report (status/name/version only)"
