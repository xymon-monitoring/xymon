#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-drop-unloadable-hostscfg.sh
#
# trimhistory judges a history file an orphan by looking its host up in the
# configuration it just loaded. When that load fails there is nothing to look
# anything up in -- so every host is "absent" and --drop deletes the whole
# XYMONHISTDIR.
#
# load_hostnames() says so plainly: it returns -1 and logs "Cannot load host
# data". trimhistory is the only caller that discards that return value
# (xymond/trimhistory.c:429); xymongen (loadlayout.c:434), xymonnet
# (xymonnet.c:404) and web/svcstatus.c:234 all check it and bail out -- and
# none of them deletes files on the answer.
#
# Two ways an admin gets there, and they fail differently:
#
#   - HOSTSCFG names a file that is not there (a typo, a path that moved): the
#     load returns -1 and says "Cannot load host data";
#   - HOSTSCFG names a directory: fopen() succeeds on Linux, the read yields
#     nothing, and the load reports *success* with an empty host list -- no
#     message at all. An empty regular file arrives at the same empty list on
#     every platform, and is checked as well: where fopen() refuses a
#     directory the run takes the failed-load branch instead, and the
#     empty-list refusal would go untested.
#
# So "the load failed" is not a sufficient guard on its own: an empty host list
# must not authorise deletion either. A site that really has no hosts loses
# nothing by being refused, since there is nothing to keep or trim.
#
# Distinct from #507, which is about the *network* load reporting success when
# nobody was asked. Here the load correctly reports failure and trimhistory
# proceeds regardless, so this stands whatever happens to the lib.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/trimhistory.sh
. "$(dirname "$0")/../lib/trimhistory.sh"

work=$(mktempdir)
setup_trimhistory "$work"
hosts_cfg "$work" '127.0.0.1 active.example.com # conn'

seed_files() {
	seed_hosthistory "$work" active.example.com
	seed_svchistory  "$work" active,example,com.conn
}

# ---- HOSTSCFG names a file that does not exist ------------------------------
seed_files
export TRIM_HOSTSCFG="!$work/etc/nosuchfile.cfg"
rc=0
run_trimhistory "$work" --drop >"$work/missing.log" 2>&1 || rc=$?
log=$(cat "$work/missing.log")

assert_contains "Cannot load host data" "$log" \
	"a failed configuration load was not reported"
assert_file_exists "$work/var/hist/active.example.com" \
	"--drop deleted a listed host's history after the configuration failed to load"
assert_file_exists "$work/var/hist/active,example,com.conn" \
	"--drop deleted a listed host's service history after the configuration failed to load"
[ "$rc" -ne 0 ] \
	|| fail "trimhistory reported success after failing to load the configuration it needs to decide what to delete"

# ---- without --drop, a failed load still has work it can do safely ----------
# "allevents" is the all-hosts event log: it is recognised by name before any
# host lookup, so trimming it needs no host list. Refusing to run at all would
# take that away for no gain -- nothing is deleted in this mode.
rm -f "${work:?}/var/hist/"*
seed_files
{
	printf 'ancient.example.com conn %d %d 3600 purple red 0\n' "$TRIM_ANCIENT" "$TRIM_ANCIENT"
	printf 'old.example.com conn %d %d 3600 red green 0\n' "$TRIM_OLD" "$TRIM_OLD"
	printf 'active.example.com conn %d %d 3600 green red 0\n' "$TRIM_RECENT" "$TRIM_RECENT"
} >"$work/var/hist/allevents"
export TRIM_HOSTSCFG="!$work/etc/nosuchfile.cfg"
rc=0
run_trimhistory "$work" >"$work/nodrop.log" 2>&1 || rc=$?

[ "$rc" -eq 0 ] \
	|| fail "a run without --drop failed after a failed load, though trimming allevents needs no host list"

assert_file_exists "$work/var/hist/active.example.com" \
	"a run without --drop deleted a history file after a failed load"
events=$(cat "$work/var/hist/allevents")
assert_contains "$TRIM_RECENT" "$events" "allevents lost its post-cutoff event"
assert_not_contains "$TRIM_ANCIENT" "$events" \
	"allevents was not trimmed after a failed load, though trimming it needs no host list"

# ---- HOSTSCFG names a directory: the load "succeeds" with nothing in it ------
rm -f "${work:?}/var/hist/"*
seed_files
export TRIM_HOSTSCFG="!$work/etc"
rc=0
run_trimhistory "$work" --drop >"$work/emptyload.log" 2>&1 || rc=$?

assert_file_exists "$work/var/hist/active.example.com" \
	"--drop emptied the history directory on a host list that loaded with no hosts in it"
assert_file_exists "$work/var/hist/active,example,com.conn" \
	"--drop deleted a service history on a host list that loaded with no hosts in it"
[ "$rc" -ne 0 ] \
	|| fail "trimhistory reported success after loading a host list with no hosts in it"

# ---- HOSTSCFG names an empty file: the same empty list, on every platform ----
# The directory above rests on fopen() accepting a directory. Where it does not,
# that run is a *failed* load and takes the branch above, leaving the empty-list
# refusal unexercised. An empty regular file loads successfully with no hosts
# anywhere, so the message check below is what says which branch stopped the run.
rm -f "${work:?}/var/hist/"*
seed_files
: >"$work/etc/empty.cfg"
export TRIM_HOSTSCFG="!$work/etc/empty.cfg"
rc=0
run_trimhistory "$work" --drop >"$work/emptyfile.log" 2>&1 || rc=$?

assert_not_contains "Cannot load host data" "$(cat "$work/emptyfile.log")" \
	"an empty hosts.cfg was reported as a failed load -- the empty-list refusal is not what stopped the run"
assert_file_exists "$work/var/hist/active.example.com" \
	"--drop emptied the history directory on an empty hosts.cfg"
assert_file_exists "$work/var/hist/active,example,com.conn" \
	"--drop deleted a service history on an empty hosts.cfg"
[ "$rc" -ne 0 ] \
	|| fail "trimhistory reported success after loading an empty hosts.cfg"

echo "OK $(basename "$0")"
