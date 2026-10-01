#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-drop-needs-hostlist.sh
#
# "trimhistory --drop" decides what to delete by asking whether each history
# file's host is in hosts.cfg. If the host list did not load, every host is
# absent and every file is an orphan -- so a load that quietly produces
# nothing must never be allowed to authorise a deletion.
#
# It currently is. load_hostnames() asks xymond for the configuration whenever
# the filename it is handed equals $HOSTSCFG (lib/loadhosts_file.c), which is
# exactly what trimhistory passes it. When XYMSRV resolves to an empty string
# there is no recipient to ask: sendmessage() hands sendtomany() an empty
# list, whose loop body never runs, and it returns XYMONSEND_OK with an empty
# response. prepare_fromnet() therefore does not return -1, the file-load
# fallback never runs, the host list stays empty, and --drop deletes the whole
# history directory without printing one error.
#
# XYMSRV defaults to $XYMONSERVERIP, which defaults to the XYMONHOSTIP the
# tree was built with, so whether an unset environment lands here depends on
# the build. The test sets XYMSRV empty itself rather than inheriting that:
# the trigger under test is the empty recipient, and it should be stated.
#
# The same run with XYMSRV pointing at a refused port behaves correctly:
# sendmessage fails, prepare_fromnet() returns -1, the file is read, and the
# host is recognised. That contrast is asserted here too, so a fix cannot be
# mistaken for the environment simply never reaching the network path.
#
# This test follows the bug to its consequence, three layers from where it
# lives; tests/server/sendmsg-empty-recipient.sh checks sendtomany() itself.
# Both, because each covers what the other cannot: an end-to-end test fails
# when any link in the chain breaks, including links nobody has touched yet,
# while a test at the defect cannot be blinded by a change somewhere else.
#
# That is not hypothetical here. Asserting that the history files survive -- the
# obvious way to write this -- stops being evidence as soon as trimhistory
# refuses to drop on an empty host list (#281), because then they survive
# either way. So the assertions below are about the work the run did, not about
# the damage it avoided: a fixture record old enough for any run to trim is
# gone only if the host list really loaded.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/trimhistory.sh
. "$(dirname "$0")/../lib/trimhistory.sh"

work=$(mktempdir)
setup_trimhistory "$work"
hosts_cfg "$work" '127.0.0.1 active.example.com # conn'

# The bug is an empty recipient, not an absent daemon: no connection is
# attempted at all, so the result does not depend on whether this machine
# happens to run a xymond.
export XYMSRV=""
unset XYMONSERVERIP XYMSERVERS

# A bare path is what a real install has in xymonserver.cfg, and it is the
# spelling that takes the xymond-first route.
export TRIM_HOSTSCFG="$work/etc/hosts.cfg"

seed_hosthistory "$work" active.example.com
seed_svchistory  "$work" active,example,com.conn

rc=0
run_trimhistory "$work" --drop >"$work/out.log" 2>&1 || rc=$?
log=$(cat "$work/out.log")

assert_file_exists "$work/var/hist/active.example.com" \
	"--drop deleted the history of a host that IS in hosts.cfg, because the host list loaded empty and nothing said so"
assert_file_exists "$work/var/hist/active,example,com.conn" \
	"--drop deleted the service history of a host that IS in hosts.cfg, because the host list loaded empty"

# The files surviving is not evidence on its own. #281's fix refuses to drop at
# all on an empty host list, so once that is in the tree they survive whether or
# not the send was refused -- and a test that only asks whether anything was
# deleted stops saying anything about this bug.
#
# What only a working file fallback produces is a run that went ahead and did
# its work. The fixture carries a record old enough for any run to trim, so the
# trimming is the evidence: present means the host list loaded and the host was
# found, absent means trimhistory never got that far.
events=$(cat "$work/var/hist/active.example.com")
assert_contains "$TRIM_RECENT" "$events" \
	"the host history lost its post-cutoff record"
assert_not_contains "$TRIM_ANCIENT" "$events" \
	"nothing was trimmed -- the host list never loaded, so there was no host to work on"
assert_not_contains "refusing to drop" "$log" \
	"the run was stopped by the empty-host-list guard, so the file fallback never happened"
[ "$rc" -eq 0 ] \
	|| fail "a run whose hosts.cfg is readable did not complete: $log"

# Control: the same path with a reachable-but-refused server already falls
# back to the file correctly, so the fixture is not simply avoiding the net.
work2=$(mktempdir)
setup_trimhistory "$work2"
hosts_cfg "$work2" '127.0.0.1 active.example.com # conn'
seed_hosthistory "$work2" active.example.com
export XYMSRV=127.0.0.1 XYMONDPORT=1
run_trimhistory "$work2" --drop >"$work2/out.log" 2>&1 || true
assert_file_exists "$work2/var/hist/active.example.com" \
	"a refused xymond connection did not fall back to the hosts.cfg file"
assert_contains "Cannot load hosts.cfg from xymond" "$(cat "$work2/out.log")" \
	"a refused xymond connection was not reported"

echo "OK $(basename "$0")"
