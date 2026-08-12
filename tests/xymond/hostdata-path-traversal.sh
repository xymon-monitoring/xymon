#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-path-traversal.sh
#
# Regression guard: a host name from a channel message must not let
# xymond_hostdata escape the client-log directory through the two paths that
# do not merely write a file. @@drophost recursively deletes
# "$CLIENTLOGS/<host>", and @@renamehost renames it - and with
# --ghosts=allow the name is whatever the client sent, so an unconfined ".."
# takes out or moves the whole of XYMONVAR. The confinement is
# confine_name() in the worker.
#
# Scope: hostdata-hostname-sanitize.sh already covers the @@clichg save path
# for a hostile host name and a hostile test name. This one covers what that
# leaves: the delete and the rename, the rename *destination* as well as its
# source, and the degenerate "." and "/" spellings.
#
# Every assertion is "the tree did not change", compared over the whole
# scratch directory: an escape can land beside XYMONVAR ("../..") or inside
# the client-log root ("."), and a whole-tree comparison never has to predict
# which. The comparison covers both the path set and the contents of every
# file under var, because a traversal that overwrites an existing file in
# place - "$CLIENTLOGS/../CANARY.txt" - moves no path at all.
#
# Every case below is chosen so that it *can* fail: each was measured against
# a worker with confine_name() removed and confirmed to change the tree. A
# hostile spelling that the kernel rejects on its own (see the renamehost
# notes) proves nothing about the guard and is not listed.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-hostdata.sh
. "$(dirname "$0")/../lib/xymond-hostdata.sh"

# dropdirectory() forks (lib/files.c), so the delete outlives the worker and
# settle() has to wait for the child. There is no PID to wait on - the shell
# never forked it - so the wait matches on the command line, which needs
# pgrep. Declare it: without this the wait below would degrade to nothing and
# the assertions would race the delete.
command -v pgrep >/dev/null 2>&1 \
	|| skip "pgrep not available (needed to wait for the backgrounded drophost child)"

work=$(mktempdir)

setup_xymond_hostdata "$work"

seed() {  # a pristine XYMONVAR with canaries inside and outside the client-log dir
	rm -rf "${work:?}/var"
	mkdir -p "$work/var/hostdata/realhost" "$work/var/rrd" "$work/var/realhost"
	printf 'CANARY-OUTSIDE-CLIENTLOGS\n' >"$work/var/CANARY.txt"
	printf 'existing client data\n'      >"$work/var/hostdata/realhost/1785830400"
	printf 'rrd payload\n'               >"$work/var/rrd/some.rrd"
	# A decoy sharing the real host's name one level up, so "../realhost"
	# names something that exists. Without it that spelling resolves to a
	# missing directory, dropdirectory() removes nothing, and the case holds
	# whether or not the name was confined.
	printf 'decoy above the client-log root\n' >"$work/var/realhost/decoy"
	: >"$work/worker.log"
}

# Paths *and* contents. `find | sort` alone cannot see an escape that opens an
# existing file and rewrites it. worker.log lives above var and is appended to
# by every feed(), so it is outside the content half by construction.
# `|| true`: find exits 1 if an entry disappears under it, and pipefail would
# turn that into a bare errexit abort with no verdict line.
snapshot() {
	{ find "$work" 2>/dev/null || true; } | sort
	{ find "$work/var" -type f -exec cksum {} + 2>/dev/null || true; } | sort
}

feed() {  # feed <message-first-line> [body]
	local rc
	set +e
	printf '%s\n%s\n@@\n' "$1" "${2:-payload}" \
		| run_xymond_hostdata_keepvar "$work" >/dev/null 2>>"$work/worker.log"
	rc=$?
	set -e
	# The worker exits 0 when its input pipe closes. Anything else is a crash
	# or an early abort, and must not be mistaken for "the guard held": a
	# worker that died on this message would satisfy every "nothing happened"
	# assertion below without confine_name() being reached at all.
	[ "$rc" -eq 0 ] || {
		cat "$work/worker.log" >&2
		fail "xymond_hostdata exited $rc (crash or early abort?) on: $1"
	}
}

# Wait for the forked delete to finish. pgrep exits 0 with a match, 1 with
# none, and >1 on its own errors - which must not be read as "the child is
# gone", or the assertions would race a delete that is still running and
# "nothing changed" would mean "not yet".
settle() {
	local n=0 rc
	while [ $n -lt 200 ]; do
		set +e; pgrep -f "$work/xymond_hostdata" >/dev/null 2>&1; rc=$?; set -e
		[ "$rc" -eq 1 ] && return 0
		[ "$rc" -eq 0 ] || fail "pgrep failed (exit $rc) while waiting for the drophost child"
		sleep 0.05; n=$((n+1))
	done
	fail "the backgrounded drophost child did not exit within 10s"
}

unchanged() {  # unchanged <before> <label>
	local before=$1 label=$2 after
	after=$(snapshot)
	[ "$before" = "$after" ] && return 0
	# `|| true` on the pipeline: diff exits 1 when the two differ, which is
	# exactly this branch, and pipefail+errexit would abort the script here -
	# before fail() below could name the case that broke.
	{ diff <(printf '%s\n' "$before") <(printf '%s\n' "$after") \
		| sed -n 's/^> /  created: /p; s/^< /  removed: /p' || true; } >&2
	echo "--- worker stderr ---" >&2; cat "$work/worker.log" >&2
	fail "$label changed the tree outside its host directory"
}

# ---- sanity: the write path must actually work here --------------------------
# Every assertion below is of the form "nothing happened". If the worker cannot
# act at all in this environment, they all hold vacuously -- so prove the write
# path works before trusting a single refusal.
seed
feed "@@clichg#1|1785830400|10.0.0.99|realhost|1785830500|linux" "sanity payload"
if [ ! -f "$work/var/hostdata/realhost/1785830500" ]; then
	echo "--- worker stderr ---" >&2; cat "$work/worker.log" >&2
	echo "--- tree under \$work/var ---" >&2; find "$work/var" >&2
	fail "xymond_hostdata wrote nothing for a legitimate host -- the refusals below would pass vacuously"
fi

# ---- @@drophost must not delete outside the client-log directory -------------
# This one deletes recursively, so an unconfined ".." takes out all of XYMONVAR.
# "../realhost" needs the decoy seed() plants above the root; the other four
# resolve to directories that exist in every run.
for host in ".." "../.." "/" "." "../realhost"; do
	seed; before=$(snapshot)
	feed "@@drophost#1|1785830400|10.0.0.99|$host"
	settle
	unchanged "$before" "drophost with hostname '$host'"
done

# ---- @@renamehost must not move anything in or out of the directory ----------
# Both fields are confined, so both are hostile here in turn. rename(2) is
# fussy about what it will attempt at all, and a spelling it refuses on its own
# says nothing about confine_name():
#
#   - a source that is an ancestor of the destination ("..", ".", "/",
#     "../..") is EINVAL - a directory cannot move inside itself;
#   - a destination that is an existing non-empty directory ("..", ".", "/")
#     is ENOTEMPTY.
#
# So the hostile sources below all name something that exists beside the
# client-log root, and the hostile destinations all name something that does
# not exist yet. Each was checked against a guard-less worker and does move.
for src in "../rrd" "../CANARY.txt" "../realhost"; do
	seed; before=$(snapshot)
	feed "@@renamehost#1|1785830400|10.0.0.99|$src|stolen"
	unchanged "$before" "renamehost from '$src'"
done
for dst in "../stolen" "../../stolen" "/stolen" "./stolen"; do
	seed; before=$(snapshot)
	feed "@@renamehost#1|1785830400|10.0.0.99|realhost|$dst"
	unchanged "$before" "renamehost to '$dst'"
done

# ---- @@clichg: the degenerate host spellings ----------------------------------
# hostdata-hostname-sanitize.sh covers '/'-bearing and bare ".." names for both
# the host and the test field. What it does not spell is "." and "/", which
# collapse onto the client-log root rather than escaping above it - a different
# rejection to get wrong, and one that still lands a file where a host
# directory belongs.
#
# Only the host field is exercised here. The same spellings in the *test* field
# resolve the save onto an existing directory ("$CLIENTLOGS/realhost/." and
# "/../.." both name a directory, not a new file), so open() fails whether or
# not the name was confined: the assertion would hold against a worker with the
# guard removed and prove nothing. That field's guard is pinned by
# hostdata-hostname-sanitize.sh, whose "../../../escape2" does reach a
# creatable path - confirmed by removing the guard and watching it fail.
for host in "." "/" "../.."; do
	seed; before=$(snapshot)
	feed "@@clichg#1|1785830400|10.0.0.99|$host|9999999999|linux" "escaped payload"
	unchanged "$before" "clichg with hostname '$host'"
done

# ---- legitimate traffic still works -----------------------------------------
# The guards must not have turned into a blanket refusal.
seed
feed "@@renamehost#1|1785830400|10.0.0.99|realhost|renamedhost"
[ -d "$work/var/hostdata/renamedhost" ] || fail "legitimate renamehost no longer works"
[ ! -e "$work/var/hostdata/realhost" ]  || fail "legitimate renamehost left the old directory"

seed
feed "@@drophost#1|1785830400|10.0.0.99|realhost"
settle
[ ! -e "$work/var/hostdata/realhost" ] || fail "legitimate drophost no longer works"
[ -f "$work/var/CANARY.txt" ]          || fail "legitimate drophost deleted too much"
[ -f "$work/var/rrd/some.rrd" ]        || fail "legitimate drophost deleted \$XYMONVAR/rrd"
[ -f "$work/var/realhost/decoy" ]      || fail "legitimate drophost deleted the same-named directory above the root"

pass "channel host names cannot make xymond_hostdata delete or rename outside the client-log directory (#283)"
