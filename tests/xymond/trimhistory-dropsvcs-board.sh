#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-dropsvcs-board.sh
#
# "trimhistory --dropsvcs" deletes the service-history files of tests that
# xymond is no longer tracking. It asks the running xymond for the current
# board (validstatus() -> "xymondboard fields=hostname,testname") and drops
# any host+test pair the board does not carry.
#
# That board lists what is being monitored *now*, which is why this test
# belongs with #281: once trimhistory stops treating a NOTBEFORE:/NOTAFTER:
# host as absent, its service files stop being dropped as "no host" and start
# reaching this second gate instead -- where the board cannot list them
# either, precisely because the host is outside its window. Fixing the host
# lookup alone would move the deletion rather than prevent it, so the rule the
# fix has to honour is pinned here: a host that hosts.cfg lists but that is
# outside its period is not a host --dropsvcs may judge.
#
# A fake xymond stands in for the real one; validstatus() exits(1) without a
# board, so this cannot be tested by mocking nothing.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/trimhistory.sh
. "$(dirname "$0")/../lib/trimhistory.sh"

work=$(mktempdir)
setup_trimhistory "$work"
require_cc

hosts_cfg "$work" \
	'127.0.0.1 active.example.com       # conn http://example.com/' \
	'127.0.0.1 scheduledout.example.com # conn NOTAFTER:202001010000'

# The board a live xymond would return: the active host's conn test only.
# "http" was removed from that host, and the scheduled-out host is not being
# monitored at all, so neither appears.
printf 'active.example.com|conn\nactive.example.com|web.grp\n' >"$work/board.reply"

"$CC" -o "$work/fake-xymond" "$(find_root)/tests/lib/fake-xymond.c" 2>"$work/cc-fake.log" \
	|| { cat "$work/cc-fake.log" >&2; fail "fake-xymond responder does not compile"; }
"$work/fake-xymond" "$work/board.reply" >"$work/fake-xymond.port" &
fakepid=$!
register_cleanup "kill $fakepid 2>/dev/null || true"
for _ in 1 2 3 4 5 6 7 8 9 10; do
	[ -s "$work/fake-xymond.port" ] && break
	sleep 0.2
done
[ -s "$work/fake-xymond.port" ] || fail "fake-xymond did not report its port"
export XYMSRV=127.0.0.1 XYMONDPORT="$(cat "$work/fake-xymond.port")"

seed_svchistory "$work" active,example,com.conn         # on the board
seed_svchistory "$work" active,example,com.web.grp      # on the board, dotted column
seed_svchistory "$work" active,example,com.old.grp      # dotted column, retired
seed_svchistory "$work" active,example,com.http         # listed host, test retired
seed_svchistory "$work" scheduledout,example,com.conn   # listed host, outside its window
seed_svchistory "$work" scheduledout,example,com.web.grp # ... and with a dotted column
seed_hosthistory "$work" active.example.com
seed_hosthistory "$work" scheduledout.example.com

run_trimhistory "$work" --drop --dropsvcs >"$work/out.log" 2>&1 || true
log=$(cat "$work/out.log")

# The feature itself: a test the board no longer carries goes.
[ ! -e "$work/var/hist/active,example,com.http" ] \
	|| { echo "$log" >&2; fail "--dropsvcs kept a service-history file for a test the board does not list"; }
assert_contains "active,example,com.http - no service" "$log" \
	"--dropsvcs deleted a retired service file without reporting it"

# What it must not touch: a test that is on the board.
assert_file_exists "$work/var/hist/active,example,com.conn" \
	"--dropsvcs deleted the history of a service the board lists"

# The board is asked about the whole column name. Splitting the file name at
# its last dot asks about "grp", which the board does not carry, so a live
# dotted column looks retired.
assert_file_exists "$work/var/hist/active,example,com.web.grp" \
	"--dropsvcs deleted a dotted column that IS on the board -- it was asked about under a truncated name"

# And a dotted column that really is retired must be reported as a missing
# service, not as a missing host.
[ ! -e "$work/var/hist/active,example,com.old.grp" ] \
	|| fail "--dropsvcs kept a dotted column the board does not list"
assert_contains "old.grp - no service" "$log" \
	"a retired dotted column was reported as a missing host rather than a missing service"

# And a host that is listed but outside its NOTBEFORE/NOTAFTER window is not
# a host --dropsvcs gets to judge: the board omits it because it is scheduled
# out, not because the service was retired.
[ -e "$work/var/hist/scheduledout,example,com.conn" ] \
	|| { echo "$log" >&2; fail "--dropsvcs deleted the service history of a scheduled-out host, which hosts.cfg still lists (#281)"; }
assert_file_exists "$work/var/hist/scheduledout.example.com" \
	"--drop deleted the host history of a scheduled-out host (#281)"
assert_file_exists "$work/var/hist/scheduledout,example,com.web.grp" \
	"--dropsvcs deleted a dotted column of a scheduled-out host -- both gates have to hold at once"

echo "OK $(basename "$0")"
