#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/history-drop-confine.sh
#
# xymond_history builds paths under histdir/histlogdir from the hostname and
# testname carried on the drop/rename messages, neither of which xymond
# validates as a path component. The dot-to-underscore pass neutralises "." and
# ".." in a hostname, but the testname is used raw -- so
#
#     droptest <host> ../../<dir>
#
# reached dropdirectory() with a path that climbs out of histlogdir and
# recursively removed it (#287, the class the xymond_filestore hardening
# closed). This drives the real worker and checks the filesystem: an escaping
# name must be refused, and a legitimate drop must still work.
#
# Workers read their messages from stdin (xymond_worker.c).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

ROOT=$(find_root)

work=$(mktempdir)
build_xymond_worker "$work" xymond_history \
	xymond/xymond_history.c xymond/xymond_worker.c

mkdir -p "$work/etc"
cp "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"

# A drop/rename runs dropdirectory() in a forked child; wait for it before
# inspecting the filesystem.
drive() {  # drive <message-without-trailing-@@>
	printf '%s\n@@\n' "$1" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_history" --env="$work/etc/xymonserver.cfg" \
		--histdir="$work/var/hist" --histlogdir="$work/var/histlogs" \
		>/dev/null 2>>"$work/worker.log"
	# dropdirectory(background=1) forks; give the child a moment to finish.
	sleep 0.3
}

setup() {
	rm -rf "$work/var"
	mkdir -p "$work/var/hist" "$work/var/histlogs/myhost/conn"
	printf 'histlog\n' >"$work/var/histlogs/myhost/conn/10000000"
	printf 'STATUS-EVENTS\n' >"$work/var/hist/myhost.conn"
	# A directory one level above histlogdir, the target an escaping name climbs to.
	mkdir -p "$work/var/SIBLING"
	printf 'PRECIOUS\n' >"$work/var/SIBLING/keep"
}

# ---- sanity: a legitimate droptest removes exactly its own histlog ----------
setup
: >"$work/worker.log"
drive '@@droptest#1|1|10.0.0.1|myhost|conn'
[ ! -e "$work/var/histlogs/myhost/conn" ] \
	|| { cat "$work/worker.log" >&2; fail "a legitimate droptest did not remove the test's histlog dir"; }
[ ! -e "$work/var/hist/myhost.conn" ] \
	|| fail "a legitimate droptest did not remove the status-event file"

# ---- an escaping testname must not climb out of histlogdir ------------------
# droptest <host> ../../SIBLING -> dropdirectory(histlogs/myhost/../../SIBLING)
# i.e. $work/var/SIBLING, one level above histlogdir. Must be refused.
setup
: >"$work/worker.log"
drive '@@droptest#1|1|10.0.0.1|myhost|../../SIBLING'
[ -e "$work/var/SIBLING/keep" ] \
	|| fail "droptest with an escaping testname recursively removed a directory above histlogdir"
grep -qE 'begins with a dot|path separator' "$work/worker.log" \
	|| fail "an escaping droptest testname was not refused by name"

# ---- the same escape via renametest, and via a leading-dot testname ---------
setup
: >"$work/worker.log"
drive '@@renametest#1|1|10.0.0.1|myhost|conn|../../SIBLING/pwned'
[ -e "$work/var/SIBLING/keep" ] && [ ! -e "$work/var/SIBLING/pwned" ] \
	|| fail "renametest with an escaping newtestname moved a path out of histlogdir"
grep -qE 'begins with a dot|path separator' "$work/worker.log" \
	|| fail "an escaping renametest name was not refused by name"

setup
: >"$work/worker.log"
drive '@@droptest#1|1|10.0.0.1|myhost|.hidden'
grep -q 'begins with a dot' "$work/worker.log" \
	|| fail "a leading-dot testname was not refused by name"

# ---- a testname holding a separator (no leading dot) hits the other branch --
setup
: >"$work/worker.log"
drive '@@droptest#1|1|10.0.0.1|myhost|foo/bar'
[ -e "$work/var/histlogs/myhost/conn" ] \
	|| fail "a droptest refused for a bad testname still dropped the wrong dir"
grep -q 'path separator' "$work/worker.log" \
	|| fail "a testname holding '/' was not refused by name"

# ---- drophost / renamehost reject a hostname holding a separator ------------
setup
: >"$work/worker.log"
drive '@@drophost#1|1|10.0.0.1|../../SIBLING'
[ -e "$work/var/SIBLING/keep" ] \
	|| fail "drophost with an escaping hostname removed a directory above histlogdir"
grep -qE 'begins with a dot|path separator' "$work/worker.log" \
	|| fail "an escaping drophost hostname was not refused by name"

setup
: >"$work/worker.log"
drive '@@renamehost#1|1|10.0.0.1|myhost|../../SIBLING'
[ -e "$work/var/SIBLING/keep" ] \
	|| fail "renamehost with an escaping newhostname touched a path above histlogdir"
grep -qE 'begins with a dot|path separator' "$work/worker.log" \
	|| fail "an escaping renamehost name was not refused by name"

# ---- a status hostname holding a separator must not escape histdir ----------
# The host-events writer builds histdir/<hostname> with the hostname raw, so a
# configured name like "../pwned" (xymond warns but still monitors it) wrote a
# file above histdir. name_confined() on the stachg path refuses it; every real
# host name (dots allowed, no leading dot, no separator) still records.
setup
: >"$work/worker.log"
drive '@@stachg#1|1|10.0.0.1|origin|../pwned|conn|0|red|1|red|1000|green|0|0|0|0'
[ ! -e "$work/var/pwned" ] \
	|| fail "a status hostname of '../pwned' wrote a host-events file outside histdir"
grep -q 'begins with a dot' "$work/worker.log" \
	|| fail "an escaping status hostname was not refused by name"

# A legitimate dotted hostname must still record (confinement must not over-refuse).
setup
: >"$work/worker.log"
drive '@@stachg#1|1|10.0.0.1|origin|www.example.com|conn|0|red|1|red|1000|green|0|0|0|0'
[ -e "$work/var/hist/www,example,com.conn" ] \
	|| { cat "$work/worker.log" >&2; fail "a legitimate dotted hostname was refused - name_confined over-refuses"; }

# ---- an over-long status testname must not overflow the path buffers --------
# xymond caps neither a testname's length nor a '/' in it, so a ~5000-char
# testname reaches the statusevents/histlog/hostevents writers on the stachg
# channel. Unbounded, the sprintf() into their char[PATH_MAX] buffers smashed
# the stack; bounded, the write is skipped and the worker survives.
setup
: >"$work/worker.log"
big=$(awk 'BEGIN { while (i++ < 5000) printf "a" }')
set +e
printf '@@stachg#1|1|10.0.0.1|origin|myhost|%s|0|red|1|red|1000|green|0|0|0|0\nBODY\n@@\n' "$big" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_history" --env="$work/etc/xymonserver.cfg" \
		--histdir="$work/var/hist" --histlogdir="$work/var/histlogs" \
		>/dev/null 2>>"$work/worker.log"
rc=$?
set -e
[ "$rc" -le 1 ] || fail "xymond_history exited $rc (overflow?) on an over-long status testname"
[ -z "$(find "$work/var/hist" "$work/var/histlogs" -name '*aaaa*')" ] \
	|| fail "an over-long testname was built into a path rather than refused"
grep -q 'does not fit' "$work/worker.log" \
	|| fail "an over-long status testname was not reported as not fitting"

echo "OK $(basename "$0")"
