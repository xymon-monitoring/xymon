#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/analysis-file-ifexist.sh
#
# Regression guard for IFEXIST being accepted as an alias for OPTIONAL in a
# FILE rule (analysis.cfg / client-local.cfg), added for compatibility with
# the long-standing Debian "27_hobbit_files_ifexist" patch.
#
# The client-config parser maps both keywords onto the same CHK_OPTIONAL flag,
# so a rule written with IFEXIST must behave exactly like one written with
# OPTIONAL: the file check is skipped (no warning) when the target file is
# absent. The parser is what the alias lives in, and `xymond_client
# --dump-config` prints CHK_OPTIONAL back as the literal word "OPTIONAL", so
# the dump is a faithful, build-driven witness of how each rule was parsed.
#
# Edge cases pinned here:
#   - IFEXIST  -> the dumped rule carries OPTIONAL   (the alias resolves)
#   - OPTIONAL -> the dumped rule carries OPTIONAL   (unchanged baseline)
#   - neither  -> the dumped rule carries NO OPTIONAL (proves OPTIONAL is not a
#                 default the dump prints for every FILE rule, so the IFEXIST
#                 assertion above cannot pass vacuously)
#
# Before the alias existed, IFEXIST was an unknown trailing token and the rule
# dumped without OPTIONAL, so assertion (1) is exactly what regressing the
# alias would break.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

# Drives the built server-side client-data processor. Skips if unbuilt; a
# CMake/autopkgtest caller that exports XYMOND_CLIENT to a real path makes a
# dangling path a failure rather than a skip (see require_bin).
require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)

cat >"$work/analysis.cfg" <<'EOF'
FILE /xymon-tests/ifexist-target  red IFEXIST
FILE /xymon-tests/optional-target red OPTIONAL
FILE /xymon-tests/plain-target    red
EOF

dump=$("$XYMOND_CLIENT" --config="$work/analysis.cfg" --dump-config 2>/dev/null) \
	|| fail "xymond_client --dump-config exited non-zero"

# Pick out the three FILE lines by their (unique) filenames.
ifexist_line=$(printf '%s\n' "$dump" | grep -F '/xymon-tests/ifexist-target')  || true
optional_line=$(printf '%s\n' "$dump" | grep -F '/xymon-tests/optional-target') || true
plain_line=$(printf '%s\n' "$dump" | grep -F '/xymon-tests/plain-target')       || true

[ -n "$ifexist_line" ]  || fail "IFEXIST rule missing from dump-config output: $dump"
[ -n "$optional_line" ] || fail "OPTIONAL rule missing from dump-config output: $dump"
[ -n "$plain_line" ]    || fail "plain FILE rule missing from dump-config output: $dump"

# (1) IFEXIST resolves to the OPTIONAL flag -- the alias itself.
assert_contains "OPTIONAL" "$ifexist_line" \
	"IFEXIST no longer maps to OPTIONAL in a FILE rule (Debian 27_hobbit_files_ifexist compatibility, #161)"

# (2) OPTIONAL keeps meaning OPTIONAL -- the baseline the alias mirrors.
assert_contains "OPTIONAL" "$optional_line" \
	"OPTIONAL keyword no longer dumps as OPTIONAL"

# (3) A FILE rule with neither keyword must NOT be marked OPTIONAL, so (1) is a
# real signal and not something the dump prints for every FILE rule.
assert_not_contains "OPTIONAL" "$plain_line" \
	"plain FILE rule is unexpectedly OPTIONAL -- the IFEXIST check would pass vacuously"

pass "IFEXIST parses as an alias for OPTIONAL in FILE rules (#161)"
