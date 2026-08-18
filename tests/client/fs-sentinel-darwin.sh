#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-sentinel-darwin.sh
#
# The remote-df sentinel wired into xymonclient-darwin.sh (#316). df_sentinel()
# itself is byte-identical to the Linux copy -- fs-sentinel-copies.sh enforces
# that, and fs-sentinel-linux.sh tests its internals (the claim race, PID reuse,
# a probe that finishes late). What is macOS's own, and what this file tests,
# is the wiring: splitting the per-path df loop in two, so a hard-blocking
# mount is probed behind the sentinel instead of by the loop, and emitting
# marker rows in the column layout each report is parsed with.
#
# Everything is driven by a mount fixture and a df stub: no real remote
# filesystem is involved, and nothing is ever killed except the stub.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup darwin XYMONCLIENT_DARWIN
command -v column >/dev/null 2>&1 || skip "column(1) not available"
grep -q '^df_sentinel()' "$SCRIPT" || fail "remote-df sentinel missing from $SCRIPT (regressed)"

DF_CALLS="$TMP/df.calls"; : > "$DF_CALLS"; export DF_CALLS
DF_REMOTE="$TMP/df.remote"; : > "$DF_REMOTE"; export DF_REMOTE

# mount(8) as FreeBSD prints it. /net is nfs -- hard-blocking, so it goes behind
# the sentinel; /ssh is a FUSE remote that stat()s safely and must keep being
# reported by the plain df.
cat > "$STUB/mount" <<'EOF'
#!/bin/sh
cat <<'MOUNT'
/dev/disk3s1s1 on / (apfs, sealed, local, read-only, journaled)
/dev/disk6s1 on /Volumes/USBHFS (hfs, local, nodev, nosuid, journaled)
remote:/exp on /net (nfs, nodev, nosuid)
remote:/exp2 on /net2 (nfs, nodev, nosuid)
remote:/exp3 on /Volumes/archive type backup (nfs, nodev, nosuid)
MOUNT
EOF
chmod +x "$STUB/mount"

# df stub. It hangs only when asked about the hard-blocking mount by name, which
# is how a real df wedges: the plain call keeps answering throughout.
cat > "$STUB/df" <<'EOF'
#!/bin/sh
echo "$*" >> "$DF_CALLS"
_inode=; _named=
for a in "$@"; do
	case "$a" in
		-P|-H) ;;
		-i) _inode=1 ;;
		/*) _named="$_named $a" ;;
		# Real df rejects what it does not know, and the log this stub writes
		# joins argv with spaces -- so "-P -H" arriving as one word looks
		# exactly like two. Rejecting it here is the only way the test can see
		# the difference between the flags being split and not.
		*) echo "df: illegal option -- $a" >&2; exit 1 ;;
	esac
done
# The loop asks about one path at a time; the sentinel names the whole
# hard-blocking set at once. Only the latter may hang.
case " $_named " in *" /net "*) _remote=1 ;; *) _remote= ;; esac
[ -n "$_remote" ] && echo $$ >> "${DF_REMOTE:-/dev/null}"
[ -n "$_remote" ] && [ -n "${DF_HANG:-}" ] && exec "$HANGBIN" "${DF_HANGTIME:-20}"
[ -n "${DF_FAIL:-}" ] && exit 1
# An exclusion applies to a named operand too: once every operand is excluded,
# df processes nothing and exits nonzero.
if [ -n "$_inode" ]; then
	printf 'Filesystem 1024-blocks Used Available Capacity iused ifree %%iused Mounted on\n'
	for a in $_named; do printf '/dev/disk 100 50 50 50%% 1000 9000 10%% %s\n' "$a"; done
	exit 0
fi
printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
for a in $_named; do printf '/dev/disk 100 50 50 50%% %s\n' "$a"; done
EOF
chmod +x "$STUB/df"

# The wedge has to look like df to ps, on every OS. A shell script named df
# reports comm "df" on Linux but "sh" on the BSDs -- the interpreter -- so the
# PID-reuse guard would decide the recorded pid is not our df and start a
# second probe. Real df is a binary, so make the stub's wedge one too: a copy
# of sleep, named df, exec'd in place (same pid, and comm becomes "df").
HANGBIN="$TMP/hangbin/df"
fsf_wedge_binary "$HANGBIN"
export HANGBIN

SNIPPET="$TMP/df-section.sh"
fsf_extract "$SNIPPET"

mkdir -p "$TMP/probe"
# The caller's assignments come last so a case can override a default.
run_cycle() {
	env DF_CALLS="$DF_CALLS" DF_REMOTE="$DF_REMOTE" \
		XYMONTMP="$TMP/probe" \
		XYMONCLIENT_FS_DF_LOCAL_ONLY=no \
		XYMONCLIENT_FS_REMOTE_DF_BUDGET=2 \
		PATH="$STUB:$PATH" \
		"$@" \
		bash "$SNIPPET" 2>/dev/null || true
}
end_stub() { pkill -P "$1" 2>/dev/null || true; kill -9 "$1" 2>/dev/null || true; }

# --- healthy: the guarded mount is reported, the safe remote one still is -----
out=$(run_cycle)
assert_contains "/net" "$out" "a healthy hard-blocking mount is reported through the sentinel"
# With real rows, not the marker: everything the sentinel hands df has to be
# something df accepts, and a probe that fails on its own arguments looks
# exactly like a server that stopped answering.
assert_not_contains "unavailable:/net" "$out" \
	"a healthy server must produce real rows, never the unavailable marker"
assert_contains "/Volumes/USBHFS" "$out" "a local filesystem is still reported by the per-path loop"
assert_contains "/dev/disk" "$out" "and the table still carries real rows"
assert_equal '0' "$(find "$TMP/probe" -type f | awk 'END { print NR }')" \
	"a probe that completed leaves no state behind"

# macOS df has no type filter: the guard is that the per-path loop never gets
# handed a hard-blocking mount. The loop asks about one path per call; the
# sentinel names the whole set in one. With two nfs mounts in the fixture the
# two are told apart -- a call naming exactly one of them is the loop reaching
# it (#316), and a call naming both is the sentinel doing its job.
assert_equal '0' "$(($(grep -c -E '^-P -[Hi] /net2?$' "$DF_CALLS")))" \
	"the per-path loop must never probe a hard-blocking mount itself"
assert_contains "-P -H /net /net2" "$(cat "$DF_CALLS")" \
	"the guarded set is probed in one call, behind the sentinel"
# A volume whose name ends in "type <word>" is not the NetBSD spelling. Reading
# it as one takes the type from the name -- here "backup" -- and the mount then
# misses the hard-blocking list entirely, so the per-path loop reaches it and
# hangs on exactly the server the sentinel exists to survive.
# ... and on the guarded line, not merely somewhere in the log: an unguarded
# mount shows up there too, probed by the per-path loop, which is the failure.
assert_contains "/Volumes/archive type backup" "$(grep '^-P -H /net ' "$DF_CALLS")" \
	"a mount point ending in \"type WORD\" must still be recognised as nfs and guarded"

# --- wedged: both reports surface the mount, each under its own probe ---------
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"
out=$(run_cycle DF_HANG=1)
df_section=$(printf '%s\n' "$out" | sed -n '/^\[df\]/,/^\[inode\]/p')
inode_section=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')
assert_contains "unavailable:/net" "$df_section" \
	"a wedged server must surface the mount in the disk report, not drop it"
assert_contains "unavailable:/net" "$inode_section" \
	"and in the inode report, whose columns are read positionally"
# Shape, not just presence: the inode awk reads iused/ifree/%iused from fields
# 6-8, so a marker row in the disk layout would be reformatted into nonsense
# that still contains the mount name.
assert_contains "100% /net" "$inode_section" \
	"the inode marker row must carry the inode columns, not the disk ones"
assert_contains "/Volumes/USBHFS" "$df_section" \
	"a wedged remote server must not stale or block the local set"
[ -f "$TMP/probe/df-probe-disk.pid" ] || fail "the wedged disk probe must be left recorded"
[ -f "$TMP/probe/df-probe-inode.pid" ] || fail "the wedged inode probe must be recorded under its own tag"

# --- still wedged: no second probe -------------------------------------------
before=$(wc -l < "$DF_CALLS")
out=$(run_cycle DF_HANG=1)
after=$(wc -l < "$DF_CALLS")
assert_equal "$((before + 2))" "$((after))" \
	"while both probes are wedged, a cycle must run only the two plain dfs"
assert_contains "unavailable:/net" "$out" "the mount stays unavailable while wedged"
for _tag in disk inode; do end_stub "$(cat "$TMP/probe/df-probe-$_tag.pid")"; done
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"

# --- the probe directory is itself on a hard-blocking mount ------------------
# Nothing may touch it: even `test -w` blocks in D-state there, inside the
# fail-safe meant to prevent exactly that.
mkdir -p "$TMP/remoteprobe"
cat > "$STUB/mount" <<EOF
#!/bin/sh
cat <<'MOUNT'
/dev/disk3s1s1 on / (apfs, sealed, local, read-only, journaled)
/dev/disk6s1 on /Volumes/USBHFS (hfs, local, nodev, nosuid, journaled)
remote:/exp on /net (nfs, nodev, nosuid)
remote:/p on $TMP/remoteprobe (nfs, nodev, nosuid)
MOUNT
EOF
chmod +x "$STUB/mount"
out=$(run_cycle DF_HANG=1 XYMONTMP="$TMP/remoteprobe")
assert_equal '0' "$(find "$TMP/remoteprobe" -type f | awk 'END { print NR }')" \
	"a probe dir on a hard-blocking filesystem must never be touched"
assert_contains "unavailable:/net" "$out" "the remote set reports unavailable instead"


# Same guard, mount list unavailable rather than hostile. Asked directly: with
# no mount list there is no remote set either, so df_sentinel is never reached
# through the block. What is reachable is a list that answers once -- long
# enough to name the remote set -- and fails on the second read in the same
# cycle. A pipeline would hand this function awk's status, and awk succeeds on
# no input, so a directory nobody could place would read as local.
( sed -n '/^probe_dir_is_local()/,/^}/p' "$SCRIPT"
  printf 'fs_mounts() { return 1; }\n'
  printf 'probe_dir_is_local /remote/probe\n' ) > "$TMP/pdl.sh"
if XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES="nfs nfs4 cifs" /bin/sh "$TMP/pdl.sh"; then
	fail "an unreadable mount list must not read as a local probe directory"
fi

pass "xymonclient-darwin.sh: the remote-df sentinel takes the hard-blocking mounts out of the per-path loop"
