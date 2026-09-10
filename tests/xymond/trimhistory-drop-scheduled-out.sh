#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-drop-scheduled-out.sh
#
# Regression guard for #281. "trimhistory --drop" is documented as deleting
# files "from hosts that are not listed in the hosts.cfg(5) file"
# (xymond/trimhistory.8). A host carrying NOTBEFORE: or NOTAFTER: *is* listed
# -- it is only outside its active period -- yet --drop deletes its history,
# so the tags that exist to schedule a host in and out of monitoring destroy
# the history of exactly the hosts that are coming back.
#
# trimhistory resolves each file's host with knownhost(..., GH_IGNORE), and
# knownhost() applies the period check to every caller except GH_ALLOW
# (lib/loadhosts.c). To trimhistory an out-of-period host is therefore
# indistinguishable from one deleted from hosts.cfg: F_DROPIT, then unlink.
#
# Three consequences, each checked below:
#   1. the history of a scheduled-out host is deleted;
#   2. its host-history file is reported as a *service*-history file -- the
#      full-name lookup fails, so the code splits on the last dot and reads
#      ".com" as a service name, which is what the operator later finds in
#      the log when looking for the deletion;
#   3. --droplogs disagrees with --drop about the same host: knownloghost()
#      carries no period check, so the histlogs directory survives while the
#      history file it belongs to is gone.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/trimhistory.sh
. "$(dirname "$0")/../lib/trimhistory.sh"

work=$(mktempdir)
setup_trimhistory "$work"

hosts_cfg "$work" \
	'127.0.0.1 active.example.com        # conn' \
	'127.0.0.1 scheduledout.example.com  # conn NOTAFTER:202001010000' \
	'127.0.0.1 notyetin.example.com      # conn NOTBEFORE:209901010000' \
	'127.0.0.1 inwindow.example.com      # conn NOTBEFORE:202001010000 NOTAFTER:209901010000' \
	'127.0.0.1 badtime.example.com       # conn NOTAFTER:notatimestamp'

# gone.example.com is deliberately absent from hosts.cfg: it is the file
# --drop is supposed to remove, and without it every "kept" assertion below
# would also hold for a run that dropped nothing at all.
for h in active scheduledout notyetin inwindow badtime gone; do
	seed_hosthistory "$work" "$h.example.com"
	seed_svchistory  "$work" "$h,example,com.conn"
	seed_histlogs    "$work" "${h}_example_com" conn
done

run_trimhistory "$work" --drop --droplogs >"$work/out.log" 2>&1 || true
log=$(cat "$work/out.log")

# Sanity first: --drop has to drop the real orphan, or "kept" proves nothing.
[ ! -e "$work/var/hist/gone.example.com" ] \
	|| { echo "$log" >&2; fail "--drop kept gone.example.com, which is not in hosts.cfg -- the test cannot tell keeping from doing nothing"; }
[ ! -e "$work/var/hist/gone,example,com.conn" ] \
	|| fail "--drop kept the service history of gone.example.com, which is not in hosts.cfg"

# And it must not touch a plainly active host.
assert_file_exists "$work/var/hist/active.example.com" \
	"--drop deleted the history of an active host"
assert_file_exists "$work/var/hist/active,example,com.conn" \
	"--drop deleted the service history of an active host"

# 1. The defect: a host that is listed but outside its window keeps its files.
for h in scheduledout notyetin; do
	[ -e "$work/var/hist/$h.example.com" ] \
		|| { echo "$log" >&2; fail "--drop deleted the host history of $h.example.com, which IS listed in hosts.cfg (#281)"; }
	[ -e "$work/var/hist/$h,example,com.conn" ] \
		|| fail "--drop deleted the service history of $h.example.com, which IS listed in hosts.cfg (#281)"
done

# A host whose window spans now is the control: it must be kept whatever
# happens to the two above.
assert_file_exists "$work/var/hist/inwindow.example.com" \
	"--drop deleted the history of a host inside its NOTBEFORE/NOTAFTER window"

# An unparsable tag value leaves the host with no window at all
# (lib/loadhosts.c turns a bad NOTAFTER: into INT_MAX and logs "Invalid
# timestring"), so it must be treated as always active, never as absent.
assert_file_exists "$work/var/hist/badtime.example.com" \
	"--drop deleted the history of a host whose NOTAFTER: value does not parse"

# The service side of those two, which the host-file assertions above do not
# reach: they travel a different path through the scan.
assert_file_exists "$work/var/hist/inwindow,example,com.conn" \
	"--drop deleted the service history of a host inside its window"
assert_file_exists "$work/var/hist/badtime,example,com.conn" \
	"--drop deleted the service history of a host whose NOTAFTER: value does not parse"

# 2. No file belonging to a listed host may be reported as orphaned at all,
#    and a host-history file must never be announced as a service file.
assert_not_contains "scheduledout" "$log" \
	"a scheduled-out host was reported as orphaned (#281)"
assert_not_contains "notyetin" "$log" \
	"a not-yet-active host was reported as orphaned (#281)"

# 3. The two scans must agree about the same host: history and histlogs are
#    kept together, or dropped together.
[ -d "$work/var/histlogs/scheduledout_example_com" ] \
	|| fail "--droplogs removed the histlogs of a listed host"
[ -e "$work/var/hist/scheduledout.example.com" ] \
	|| fail "--drop deleted the history of scheduledout.example.com while --droplogs kept its histlogs -- the two scans disagree (#281)"
[ ! -d "$work/var/histlogs/gone_example_com" ] \
	|| fail "--droplogs kept the histlogs of a host that is not in hosts.cfg"

# A kept file is trimmed, not merely left alone: the pre-cutoff line goes,
# the post-cutoff line stays.
kept=$(cat "$work/var/hist/scheduledout.example.com")
assert_contains "$TRIM_RECENT" "$kept" "the post-cutoff line was trimmed out of a kept history file"
assert_contains "$TRIM_OLD" "$kept" "the record spanning the cutoff was dropped from a kept history file"
assert_not_contains "$TRIM_ANCIENT" "$kept" "a pre-cutoff line survived in a kept history file"

echo "OK $(basename "$0")"
