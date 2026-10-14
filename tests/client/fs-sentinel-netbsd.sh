#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-sentinel-netbsd.sh
#
# The remote-df sentinel wired into xymonclient-netbsd.sh (#316). df_sentinel()
# itself is byte-identical to the Linux copy -- fs-sentinel-copies.sh enforces
# that, and fs-sentinel-linux.sh tests its internals (the claim race, PID reuse,
# a probe that finishes late). What is NetBSD's own, and what this file tests,
# is the wiring: finding the hard-blocking mounts through mount(8) instead of
# /proc/mounts, keeping them out of the unguarded df, and emitting marker rows
# in the column layout each report is parsed with.
#
# Everything is driven by a mount fixture and a df stub: no real remote
# filesystem is involved, and nothing is ever killed except the stub.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup netbsd XYMONCLIENT_NETBSD
grep -q '^df_sentinel()' "$SCRIPT" || fail "remote-df sentinel missing from $SCRIPT (regressed)"

DF_CALLS="$TMP/df.calls"; : > "$DF_CALLS"; export DF_CALLS
DF_REMOTE="$TMP/df.remote"; : > "$DF_REMOTE"; export DF_REMOTE

# mount(8) as NetBSD prints it. /net is nfs -- hard-blocking, so it goes behind
# the sentinel; /ssh is a remote that stat()s safely and must keep being
# reported by the plain df.
cat > "$STUB/mount" <<'EOF'
#!/bin/sh
cat <<'MOUNT'
/dev/ld0a on / type ffs (local)
srv:/exp on /net type nfs (nodev)
usr@h:/d on /ssh type psshfs (local)
MOUNT
EOF
chmod +x "$STUB/mount"

# df stub. It hangs only when asked about the hard-blocking mount by name, which
# is how a real df wedges: the plain call keeps answering throughout.
cat > "$STUB/df" <<'EOF'
#!/bin/sh
echo "$*" >> "$DF_CALLS"
_inode=; _named=; _ex=" "
for a in "$@"; do
	case "$a" in
		-i) _inode=1 ;;
		-t*) _l=${a#-t}; _l=${_l#no}; _ex=" $(echo "$_l" | tr ',' ' ') " ;;
		/*) _named="$_named $a" ;;
		-P|-k|-h|-H) ;;
		# Real df rejects what it does not know, and the log above joins argv
		# with spaces -- so two flags arriving as one word look exactly like
		# two flags. Rejecting the unknown word is what tells them apart.
		*) echo "df: illegal option -- $a" >&2; exit 1 ;;
	esac
done
[ -n "$_named" ] && echo $$ >> "${DF_REMOTE:-/dev/null}"
[ -n "$_named" ] && [ -n "${DF_HANG:-}" ] && exec "$HANGBIN" "${DF_HANGTIME:-20}"
[ -n "${DF_FAIL:-}" ] && exit 1
# An exclusion applies to a named operand too: once every operand is excluded,
# df processes nothing and exits nonzero.
if [ -n "$_named" ]; then
	_keep=
	for a in $_named; do
		case "$a" in /net) _t=nfs ;; /ssh) _t=psshfs ;; *) _t=ffs ;; esac
		case "$_ex" in *" $_t "*) ;; *) _keep="$_keep $a" ;; esac
	done
	[ -z "$_keep" ] && { echo "df: no file systems processed" >&2; exit 1; }
	if [ -n "$_inode" ]; then
		printf 'Filesystem 512-blocks Used Available Capacity iUsed iAvail %%iCap Mounted on\n'
		for a in $_keep; do printf 'srv:/exp 3837980 2953852 692232 81%% 41013 218057 15%% %s\n' "$a"; done
	else
		printf 'Filesystem 512-blocks Used Available Capacity Mounted on\n'
		for a in $_keep; do printf 'srv:/exp 3837980 2953852 692232 81%% %s\n' "$a"; done
	fi
	exit 0
fi
if [ -n "$_inode" ]; then
	printf 'Filesystem 512-blocks Used Available Capacity iUsed iAvail %%iCap Mounted on\n'
	printf '/dev/ld0a 3837980 2953852 692232 81%% 41013 218057 15%% /\n'
	case "$_ex" in *" psshfs "*) ;; *) printf 'usr@h:/d 3837980 2953852 692232 81%% 41013 218057 15%% /ssh\n' ;; esac
	exit 0
fi
printf 'Filesystem 512-blocks Used Available Capacity Mounted on\n'
printf '/dev/ld0a 3837980 2953852 692232 81%% /\n'
case "$_ex" in *" psshfs "*) ;; *) printf 'usr@h:/d 3837980 2953852 692232 81%% /ssh\n' ;; esac
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
		/bin/sh "$SNIPPET" 2>/dev/null || true
}
end_stub() { pkill -P "$1" 2>/dev/null || true; kill -9 "$1" 2>/dev/null || true; }

# --- healthy: the guarded mount is reported, the safe remote one still is -----
out=$(run_cycle)
assert_contains "/net" "$out" "a healthy hard-blocking mount is reported through the sentinel"
# With real rows, not the marker: everything the sentinel hands df has to be
# something df accepts, and a probe that fails on its own arguments looks
# exactly like a server that stopped answering.
assert_not_match 'srv:/exp[[:space:]]+-[[:space:]]+-[[:space:]]+-[[:space:]]+100%[[:space:]]+/net' "$out" \
	"a healthy server must produce real rows, never the unavailable marker"
assert_contains "/ssh" "$out" "a remote type that cannot hard-block stays in the plain df"
assert_contains "/dev/ld0a" "$out" "and the local set is reported as before"
assert_equal '0' "$(find "$TMP/probe" -type f | awk 'END { print NR }')" \
	"a probe that completed leaves no state behind"

# The unguarded df must never be asked to stat a hard-blocking type: the calls
# that name no mount point are the plain ones, and their -t list has to carry
# every hard-blocking type (#316).
plain_args=$(grep -v ' /net' "$DF_CALLS" | tr '\n' ' ')
assert_contains ",nfs," "$plain_args" "the plain df excludes nfs"
assert_contains ",smbfs," "$plain_args" "and smbfs"
assert_contains ",lustre" "$plain_args" "and the rest of the hard-blocking list"

# --- wedged: both reports surface the mount, each under its own probe ---------
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"
out=$(run_cycle DF_HANG=1)
df_section=$(printf '%s\n' "$out" | sed -n '/^\[df\]/,/^\[inode\]/p')
inode_section=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')
assert_match 'srv:/exp[[:space:]]+-[[:space:]]+-[[:space:]]+-[[:space:]]+100%[[:space:]]+/net' "$df_section" \
	"a wedged server must surface the mount in the disk report, not drop it"
assert_match 'srv:/exp[[:space:]]+-[[:space:]]+-[[:space:]]+-[[:space:]]+100%[[:space:]]+/net' "$inode_section" \
	"and in the inode report, whose columns are read positionally"
# Shape, not just presence: the inode awk reads iused/ifree/%iused from fields
# 6-8, so a marker row in the disk layout would be reformatted into nonsense
# that still contains the mount name.
assert_contains "100% /net" "$inode_section" \
	"the inode marker row must carry the inode columns, not the disk ones"
assert_contains "/dev/ld0a" "$df_section" \
	"a wedged remote server must not stale or block the local set"
[ -f "$TMP/probe/df-probe-disk.pid" ] || fail "the wedged disk probe must be left recorded"
[ -f "$TMP/probe/df-probe-inode.pid" ] || fail "the wedged inode probe must be recorded under its own tag"

# --- still wedged: no second probe -------------------------------------------
before=$(wc -l < "$DF_CALLS")
out=$(run_cycle DF_HANG=1)
after=$(wc -l < "$DF_CALLS")
assert_equal "$((before + 2))" "$((after))" \
	"while both probes are wedged, a cycle must run only the two plain dfs"
assert_match 'srv:/exp[[:space:]]+-[[:space:]]+-[[:space:]]+-[[:space:]]+100%[[:space:]]+/net' "$out" "the mount stays unavailable while wedged"
for _tag in disk inode; do end_stub "$(cat "$TMP/probe/df-probe-$_tag.pid")"; done
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"

# --- the probe directory is itself on a hard-blocking mount ------------------
# Nothing may touch it: even `test -w` blocks in D-state there, inside the
# fail-safe meant to prevent exactly that.
mkdir -p "$TMP/remoteprobe"
cat > "$STUB/mount" <<EOF
#!/bin/sh
cat <<'MOUNT'
/dev/ld0a on / type ffs (local)
srv:/exp on /net type nfs (nodev)
srv:/p on $TMP/remoteprobe type nfs (nodev)
MOUNT
EOF
chmod +x "$STUB/mount"
out=$(run_cycle DF_HANG=1 XYMONTMP="$TMP/remoteprobe")
assert_equal '0' "$(find "$TMP/remoteprobe" -type f | awk 'END { print NR }')" \
	"a probe dir on a hard-blocking filesystem must never be touched"
assert_match 'srv:/exp[[:space:]]+-[[:space:]]+-[[:space:]]+-[[:space:]]+100%[[:space:]]+/net' "$out" "the remote set reports unavailable instead"


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

# The probe dir must reach awk intact: -v escape-processes it (every awk turns
# \t into a tab), hence ENVIRON; the quoted heredoc keeps printf off the bytes.
( sed -n '/^probe_dir_is_local()/,/^}/p' "$SCRIPT"
  cat <<'PDL'
fs_mounts() { printf 'nfs4\t%s\n' '/remote/a\tb'; }
probe_dir_is_local '/remote/a\tb/x'
PDL
) > "$TMP/pdl.sh"
if XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES="nfs nfs4 cifs" /bin/sh "$TMP/pdl.sh"; then
	fail "a probe directory under a backslash-named mount must not read as local"
fi

pass "xymonclient-netbsd.sh: the remote-df sentinel is wired to mount(8) and both reports -- NetBSD mount output replayed, df stubbed"
