#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/rrd-path-traversal.sh
#
# Regression guard: xymond_rrd must not let a hostname taken from a channel
# message escape the RRD directory.
#
#   @@drophost   -> dropdirectory("$RRDDIR/<host>", 1)   <- recursive delete
#   @@renamehost -> rename("$RRDDIR/<old>", "$RRDDIR/<new>")
#
# drophost filtered with basename(), which returns ".." for ".." and so
# confines nothing; renamehost did not filter at all. Since the delete is
# recursive, an unconfined ".." takes out the parent of the RRD directory --
# the historical data.
#
# Workers read their messages from stdin (xymond_worker.c) and are also
# reachable over the network in a distributed setup (net_worker_run(),
# xymond_rrd.c), so the component has to be confined here rather than
# upstream in xymond.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"
[ -f "$ROOT/include/config.h" ] || skip "tree not configured (no include/config.h)"
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"

rrddef=$(sed -n 's/^RRDDEF *= *//p' "$ROOT/Makefile")
rrdlibs=$(sed -n 's/^RRDLIBS *= *//p' "$ROOT/Makefile")
[ -n "$rrdlibs" ] || skip "tree built without RRD support (no RRDLIBS)"
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")
pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-rrd-traversal.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymon.a libxymoncomm.a libxymontime.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot build the xymon libraries"; }

# RRDOBJS = xymond_rrd.o xymond_worker.o do_rrd.o client_config.o (xymond/Makefile).
# Archives listed twice rather than --start-group, which is GNU ld only.
"$CC" -I"$ROOT/include" -I"$ROOT/lib" -I"$ROOT/xymond" $rrddef -o "$work/xymond_rrd" \
	"$ROOT/xymond/xymond_rrd.c" "$ROOT/xymond/xymond_worker.c" \
	"$ROOT/xymond/do_rrd.c" "$ROOT/xymond/client_config.c" \
	"$ROOT/lib/libxymon.a" "$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymontime.a" \
	"$ROOT/lib/libxymon.a" \
	$pcre_libs $rrdlibs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "cannot link xymond_rrd here (RRD headers/libs missing)"; }

mkdir -p "$work/etc"
cp "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"

setup() {
	rm -rf "$work/var"
	mkdir -p "$work/var/rrd/realhost" "$work/var/hostdata"
	printf 'CANARY-OUTSIDE-RRDDIR\n' >"$work/var/CANARY.txt"
	printf 'rrd payload\n'           >"$work/var/rrd/realhost/tcp.rrd"
	printf 'client data\n'           >"$work/var/hostdata/keepme"
}

feed() {
	local rc
	set +e
	printf '%s\n@@\n' "$1" \
	| XYMONHOME="$work" XYMONVAR="$work/var" XYMONTMP="$work" \
		"$work/xymond_rrd" --env="$work/etc/xymonserver.cfg" \
		--rrddir="$work/var/rrd" --no-rrd --processor=/bin/cat \
		>/dev/null 2>>"$work/worker.log"
	rc=$?
	set -e
	[ "$rc" -le 1 ] || fail "xymond_rrd exited $rc (crash?) on: $1"
}

# dropdirectory() forks (lib/files.c), so the delete outlives the worker.
# Waiting matters both ways: "the canary is still there" must mean the guard
# held, not that the child had not got to it yet.
settle() {
	local n=0
	while [ $n -lt 100 ]; do
		pgrep -f "$work/xymond_rrd" >/dev/null 2>&1 || break
		sleep 0.1; n=$((n+1))
	done
	sleep 0.3
}

# ---- sanity: a legitimate drophost must actually delete -----------------------
# Every assertion below is "nothing outside was touched". If the worker does
# nothing at all here, they all hold vacuously.
setup
: >"$work/worker.log"
feed "@@drophost#1|1785830400|10.0.0.99|realhost"
settle
n=0
while [ -e "$work/var/rrd/realhost" ] && [ $n -lt 50 ]; do sleep 0.1; n=$((n+1)); done
if [ -e "$work/var/rrd/realhost" ]; then
	echo "--- worker stderr ---" >&2; cat "$work/worker.log" >&2
	echo "--- tree ---" >&2; find "$work/var" >&2
	fail "xymond_rrd did not drop a legitimate host -- the refusals below would pass vacuously"
fi

# ---- @@drophost must not delete outside the RRD directory --------------------
for host in ".." "../.." "/" "." "../.."; do
	setup
	feed "@@drophost#1|1785830400|10.0.0.99|$host"
	settle
	# Assert on the RRD directory itself, not on a sibling file. dropdirectory()
	# unlinks entries while readdir() is still walking them, which is undefined
	# for entries not yet returned -- a sibling file can be skipped by luck, and
	# an assertion resting on it passes for the wrong reason. $RRDDIR and a
	# sibling *directory* are destroyed deterministically, because directories
	# are recursed into.
	[ -d "$work/var/rrd" ] \
		|| fail "drophost with hostname '$host' deleted \$RRDDIR itself"
	[ -f "$work/var/rrd/realhost/tcp.rrd" ] \
		|| fail "drophost with hostname '$host' deleted another host's RRD data"
	[ -f "$work/var/hostdata/keepme" ] \
		|| fail "drophost with hostname '$host' deleted \$XYMONVAR/hostdata"
done

# ---- @@renamehost must not move anything in or out ---------------------------
for pair in "..|stolen" "realhost|../escaped" "..|.."; do
	old=${pair%%|*}; new=${pair##*|}
	setup
	feed "@@renamehost#1|1785830400|10.0.0.99|$old|$new"
	settle
	[ -d "$work/var/rrd" ] \
		|| fail "renamehost '$old' -> '$new' moved \$RRDDIR itself"
	[ -f "$work/var/hostdata/keepme" ] \
		|| fail "renamehost '$old' -> '$new' touched \$XYMONVAR/hostdata"
	[ ! -e "$work/var/escaped" ] \
		|| fail "renamehost '$old' -> '$new' created a directory outside \$RRDDIR"
done

# ---- legitimate rename still works -------------------------------------------
setup
feed "@@renamehost#1|1785830400|10.0.0.99|realhost|renamedhost"
settle
[ -d "$work/var/rrd/renamedhost" ] || fail "legitimate renamehost no longer works"

echo "OK $(basename "$0")"
