#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-drop-lost-include.sh
#
# A hosts.cfg says which of its includes are allowed to be missing: "optional
# include ..." may be absent, a plain "include ..." is expected to be there.
# Both were treated the same when the file would not open -- a warning, and on
# with a host list short by everything that include carried (#512).
#
# For a renderer that is a degraded page. For "trimhistory --drop" it is data
# loss: those hosts are absent from the list, so their history files look like
# orphans, and they are deleted. Nothing in the run says the configuration was
# incomplete -- the warning names a file, not the hosts it was carrying, and the
# exit status is 0.
#
# The second half is the control, and it is why the fix reads the keyword rather
# than counting includes: "optional include" must still proceed and drop what
# really is orphaned. A fix that refused to run whenever a hosts.cfg mentioned
# an include would pass the first half and be useless.
#
# HOSTSCFG carries a "!" prefix on purpose. load_hostnames() asks xymond for the
# configuration whenever the filename it is handed equals $HOSTSCFG
# (lib/loadhosts_file.c) -- which is what trimhistory passes it. With no xymond
# listening the host list would then load empty, every file would look orphaned,
# and a --drop test would "pass" by deleting the whole fixture. The "!" forces
# the file load this test is about.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

work=$(mktempdir)
build_xymond_worker "$work" trimhistory xymond/trimhistory.c
mkdir -p "$work/etc" "$work/var/hist" "$work/var/logs"

now=$(date +%s)
cutoff=$((now - 86400))
ancient=$((now - 8640000))   # before the cutoff: must be trimmed away
old=$((now - 864000))        # before it too, but the last such record is kept
recent=$((now - 3600))       # after the cutoff: must survive

# A host-history file as xymond_history writes it: "testname tstamp lastchg
# duration newcol oldcol trend", and the trimmer reads the timestamp from column
# two. Three records, because trim_history() keeps the last pre-cutoff line on
# purpose -- with a single old line a file looks untrimmed however well the
# trimming works.
seed_history() {
	{
		printf 'conn %d %d 3600 purple red 0\n' "$ancient" "$((ancient - 3600))"
		printf 'conn %d %d 3600 red green 0\n'  "$old"     "$((old - 3600))"
		printf 'conn %d %d 3600 green red 0\n'  "$recent"  "$((recent - 3600))"
	} >"$work/var/hist/$1"
}

# XYMONSERVERLOGS is redirected with the rest: trimhistory reads
# $XYMONSERVERLOGS/xymond_history.pid and signals that process, and left unset
# it falls back to the compiled-in XYMONLOGDIR -- a real Xymon's.
run_trim() {
	set +e
	XYMONHOME="$work" \
	HOSTSCFG="!$work/etc/hosts.cfg" \
	XYMONHISTDIR="$work/var/hist" \
	XYMONHISTLOGS="$work/var/histlogs" \
	XYMONSERVERLOGS="$work/var/logs" \
	XYMONTMP="$work" \
		"$work/trimhistory" --cutoff="$cutoff" "$@" >"$work/trim.log" 2>&1
	trim_rc=$?
	set -e
}

# ---- a plain include that will not open -------------------------------------
# more.cfg is the file listing fromtheinclude.example.com. It has moved, been
# renamed, or was never there.
{
	printf '127.0.0.1 active.example.com # conn\n'
	printf 'include %s/etc/more.cfg\n' "$work"
} >"$work/etc/hosts.cfg"
seed_history active.example.com
seed_history fromtheinclude.example.com

run_trim --drop
log=$(cat "$work/trim.log")

assert_file_exists "$work/var/hist/fromtheinclude.example.com" \
	"--drop deleted the history of a host hosts.cfg does list, in an include that could not be read (#512)"
assert_file_exists "$work/var/hist/active.example.com" \
	"--drop deleted the history of a host listed in hosts.cfg itself"
[ "$trim_rc" -ne 0 ] \
	|| { echo "$log" >&2; fail "trimhistory reported success after reading a hosts.cfg it could not read in full"; }
assert_contains "include" "$log" \
	"the run refused without saying that an include was the reason"

# ---- the same include, declared optional ------------------------------------
# Now the configuration says the file may be absent, so the list is complete as
# written and fromtheinclude.example.com really is a host that no longer exists.
# It has to be dropped, exactly as it was before this fix.
rm -f "${work:?}/var/hist/"*
{
	printf '127.0.0.1 active.example.com # conn\n'
	printf 'optional include %s/etc/more.cfg\n' "$work"
} >"$work/etc/hosts.cfg"
seed_history active.example.com
seed_history fromtheinclude.example.com

run_trim --drop
log=$(cat "$work/trim.log")

[ "$trim_rc" -eq 0 ] \
	|| { echo "$log" >&2; fail "--drop refused on a hosts.cfg whose include is declared optional, which is the case the keyword exists for"; }
[ ! -e "$work/var/hist/fromtheinclude.example.com" ] \
	|| { echo "$log" >&2; fail "--drop kept the history of a host nothing lists: an absent optional include still leaves a complete configuration"; }
assert_file_exists "$work/var/hist/active.example.com" \
	"--drop deleted a listed host's history on a configuration that loaded completely"

# ...and the run did its ordinary work rather than stopping early.
events=$(cat "$work/var/hist/active.example.com")
assert_contains "$recent" "$events" "the kept history lost its post-cutoff record"
assert_not_contains "$ancient" "$events" \
	"nothing was trimmed, so the run never got past the configuration load"

echo "OK $(basename "$0")"
