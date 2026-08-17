#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-mounts.sh
#
# Mount enumeration is the documented per-OS seam: /proc/mounts on Linux,
# mount(8) on the BSDs and macOS. Every other filesystem test replaces it with
# a fixture, so nothing else looks at what the real thing prints or at what the
# clients ask it for. Two things are checked here that a fixture cannot check:
#
#   1. no client asks mount(8) to refresh (static, runs on every lane)
#   2. this host's own fs_mounts() parses this host's real output (native)
#
# (1) matters because the sentinel exists for mounts whose server is gone.
# Listing them is safe -- FreeBSD's mount(8) calls getmntinfo(MNT_NOWAIT), as
# do NetBSD, OpenBSD and macOS -- but `mount -v` on FreeBSD passes MNT_WAIT,
# which stats every filesystem and blocks on exactly the dead mount the
# sentinel is trying to report. The verbose flag would look like an
# improvement in review; this is the note that says it is not.
#
# native-primitive: fs_mounts

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
TMP=$(mktempdir)

# ---------------------------------------------------------------- static ---

clients=0
for script in "$ROOT"/client/xymonclient-*.sh; do
	grep -q '^fs_mounts()' "$script" || continue
	clients=$((clients + 1))
	os=$(basename "$script" .sh)

	# Only the enumerator's own body, so an unrelated `mount -v` elsewhere in a
	# client (there is none today) would not be blamed on this rule.
	sed -n '/^fs_mounts()/,/^}/p' "$script" > "$TMP/$os"
	[ -s "$TMP/$os" ] || fail "$os: fs_mounts() is declared but its body could not be extracted -- the '}' terminator moved?"

	if grep -Eq 'mount[[:space:]]+(-[[:alnum:]]*v|--verbose)' "$TMP/$os"; then
		fail "$os: fs_mounts() runs mount verbosely, which asks the kernel to stat every filesystem (MNT_WAIT) and hangs on the dead mounts the sentinel reports"
	fi
done

[ "$clients" -gt 0 ] || fail "no client declares fs_mounts(): mount enumeration regressed out of the tree"

# ---------------------------------------------------------------- native ---

kernel=$(uname -s)
case "$kernel" in
  Linux)   os=linux ;;
  FreeBSD) os=freebsd ;;
  NetBSD)  os=netbsd ;;
  OpenBSD) os=openbsd ;;
  Darwin)  os=darwin ;;
  *) skip "no client for $kernel, so its mount output is nobody's contract" ;;
esac

script="$ROOT/client/xymonclient-$os.sh"
[ -f "$script" ] || skip "$script missing"
grep -q '^fs_mounts()' "$script" || fail "$os: fs_mounts() missing from the client this host runs"

# mount(8) lives in /sbin on the BSDs and macOS, and the client calls it by
# name: it inherits its PATH from xymonlaunch, where /sbin is present, while a
# CI shell's PATH need not be. Name the directories rather than skip -- a skip
# here left NetBSD's mount parsing unverified for a whole campaign.
case $kernel in
  Linux) ;;
  *) PATH="$PATH:/sbin:/usr/sbin" ;;
esac
command -v mount > /dev/null 2>&1 || [ "$kernel" = Linux ] \
	|| skip "no mount(8) on this host, which is what its client reads"

# Run the helper alone, against the real system, exactly as the client would.
( sed -n '/^fs_mounts()/,/^}/p' "$script"; echo 'fs_mounts' ) > "$TMP/run.sh"
if ! sh "$TMP/run.sh" > "$TMP/out" 2> "$TMP/err"; then
	printf '%s\n' "$(cat "$TMP/err")" >&2
	fail "$os: fs_mounts() failed on the host it is written for"
fi
[ ! -s "$TMP/err" ] || {
	printf '%s\n' "$(cat "$TMP/err")" >&2
	fail "$os: fs_mounts() wrote to stderr; the client's caller would leak that into the status message"
}
[ -s "$TMP/out" ] || fail "$os: fs_mounts() listed no mounts on a running system"

# One snapshot for the Linux checks below, taken now: the stat loop can grow
# the live table (stat of an automount point mounts it).
[ "$kernel" != Linux ] || cat /proc/mounts > "$TMP/mounts"

# What the helper actually produced, on failure only: a count ("listed 2 mounts
# but not /") says something is wrong without saying what, and re-running a BSD
# lane to find out costs half an hour.
dump_rows() { printf 'fs_mounts() returned:\n' >&2; sed -e 's/^/  /' "$TMP/out" >&2; }

# The rows are what the rest of the block indexes into, so check the shape
# rather than the contents: type, tab, mountpoint, and nothing else.
lineno=0
present=0
root_type=
while IFS= read -r line; do
	lineno=$((lineno + 1))
	fields=$(printf '%s' "$line" | awk -F'\t' '{ print NF }')
	[ "$fields" = 2 ] \
		|| { dump_rows; fail "$os: fs_mounts() line $lineno is not 'type<TAB>mountpoint' ($fields tab-separated fields): $line"; }

	type=${line%%	*}
	mp=${line#*	}

	# A parse that drifted by a field yields plausible-looking garbage, so
	# check what a mountpoint always is: an absolute path. Not that it exists
	# -- an unprivileged reader cannot stat every one of them (docker's
	# overlay dirs are root-only) -- and not that it is a directory, since a
	# bind mount over a file is legal and WSL ships one at /init.
	case "$mp" in
	  /*) [ ! -e "$mp" ] || present=$((present + 1)) ;;
	  *) dump_rows; fail "$os: fs_mounts() reported '$mp' as a mountpoint, which is not an absolute path: $line" ;;
	esac

	case "$type" in
	  ''|*[[:space:]]*|*'('*|*')'*|*,*)
		dump_rows
		fail "$os: fs_mounts() reported '$type' as a filesystem type, so the parse picked up the wrong part of the line: $line"
		;;
	esac

	[ "$mp" != / ] || root_type=$type
done < "$TMP/out"

# A chroot entered without pivot_root (cowbuilder, schroot: issue #395) has
# no / row in /proc/mounts. So on Linux the no-drop check is exact -- row
# counts must match -- and / is required only when the input shows it. The
# BSDs and macOS stay unconditional: getmntinfo() sees / even in a chroot.
if [ "$kernel" = Linux ]; then
	want=$(awk 'END { print NR }' "$TMP/mounts")
	[ "$lineno" -eq "$want" ] \
		|| { dump_rows; fail "$os: fs_mounts() listed $lineno mount(s) but /proc/mounts has $want, so it is dropping rows"; }
	if awk '$2 == "/" { found = 1 } END { exit !found }' "$TMP/mounts"; then
		[ -n "$root_type" ] || { dump_rows; fail "$os: /proc/mounts lists / but fs_mounts() did not, so it is mangling rows"; }
	fi
else
	[ -n "$root_type" ] || { dump_rows; fail "$os: fs_mounts() listed $lineno mount(s) but not /, so it is dropping rows"; }
fi

# Paths that all look absolute but none of which resolve would be a parse
# producing well-formed nonsense; one that resolves is enough to rule that out.
[ "$present" -gt 0 ] || { dump_rows; fail "$os: none of the $lineno mountpoints fs_mounts() reported exists"; }

# No host can be relied on to have a mount point with a space in it (Linux
# escapes it as \040), so the parser is fed such a line directly. The BSDs
# print the space raw; the " on " / " (" split keeps it.
if [ "$kernel" = Linux ]; then
	# The awk program out of the helper, run over one fixture line instead of
	# the file. Extracted rather than copied, so it is this client's parser
	# that is tested; the pattern names awk, never the path, which is what the
	# suite's own portability rules ask for.
	awkprog=$(sed -n '/^fs_mounts()/,/^}/p' "$script" | sed -n "s/^[[:space:]]*awk '\(.*\)' .*/\1/p")
	[ -n "$awkprog" ] \
		|| fail "$os: fs_mounts() no longer parses with a single awk program -- update this check with it"
	decoded=$(printf 'server:/export /remote/team\\040share\\040x nfs4 rw 0 0\n' | awk "$awkprog")
	assert_equal "nfs4	/remote/team share x" "$decoded" \
		"$os: every escaped space in a mount point is decoded, not just the first"
fi

pass "no client refreshes mount(8), and $os's fs_mounts() parses this host ($lineno mounts, / is ${root_type:-not visible from this chroot})"
