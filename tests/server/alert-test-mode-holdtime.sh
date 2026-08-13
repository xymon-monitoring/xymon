#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/alert-test-mode-holdtime.sh
#
# "xymond_alert --test" must answer for FOR= the way the daemon would.
#
# The mode exists to check an alerts.cfg without waiting for a real alert, so
# it is the one place a wrong answer is taken at face value. It builds its
# alert record with calloc and fills in the ages by hand, and colorstart -- the
# field FOR= measures -- was left at zero there while the three other
# constructors set it (from the message's lastchange, or from the checkpoint,
# or not at all for @@notify, which never reaches FOR=). Zero means 1970, so
# every FOR= looked satisfied and --test recommended an alert the daemon would
# have held back.
#
# --test models one colour held for --duration: there is no earlier colour to
# have escalated from, so the colour is as old as the event.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
ALERT=${XYMOND_ALERT:-$ROOT/xymond/xymond_alert}

[ -x "$ALERT" ] || skip "$ALERT not built or not executable"

work=$(mktempdir)
cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1  testhost  # conn
EOF
cat >"$work/alerts.cfg" <<'EOF'
HOST=testhost SERVICE=conn COLOR=red FOR=10
	MAIL admin@example.com
EOF

# --test talks to xymond first and falls back to the file; the connection
# refusal is noise here, so only stdout is read.
run() {
	env XYMONHOME="$work" XYMONTMP="$work" HOSTSCFG="$work/hosts.cfg" \
	    MACHINE=testhost MACHINEDOTS=testhost \
	    XYMONSERVERS=127.0.0.1 XYMSRV=127.0.0.1 XYMON=/bin/true \
	    "$ALERT" --config="$work/alerts.cfg" --test testhost conn \
	    --color=red --duration="$1" 2>/dev/null
}

got=$(run 5)
assert_contains "min. hold time" "$got" \
	"--test must report a red held 5 minutes as not yet satisfying FOR=10"
assert_not_contains "Mail alert with command" "$got" \
	"and must not recommend an alert the daemon would hold back"

got=$(run 15)
assert_contains "Mail alert with command" "$got" \
	"--test must recommend the alert once the colour has held long enough"

pass "xymond_alert --test measures FOR= against the colour age it was given"
