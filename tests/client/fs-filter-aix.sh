#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-aix.sh
#
# xymonclient-aix.sh under the XYMONCLIENT_FS_* contract (#170), plus what is
# AIX's alone. AIX df has no type exclusion and names no filesystem types at
# all, so the client reads the vfs column of mount(8) and drops excluded types
# from df's *output*, keyed on the device+mountpoint pair.
#
# The shared contract lives in fs-filter-common.sh and is asserted on the
# emitted sections. Two of its rules are opted out of with
# FSF_INODE_FILTERED=no: the AIX inode report carries no type filter, only its
# own "no usable count" guard, which xymonclient.cfg(5) documents.
#
# What the shared fixture cannot express is asserted below it, against a
# fixture built for it: the pair keying (two mounts at one mount point), the
# remote row layout in mount(8) (a leading node column, the device rendering as
# node:path in df), "-T local" as two arguments, and the -l on the System V df
# behind [inode]. That df is an absolute path a PATH stub cannot reach, so the
# extracted block is repointed at one with sed, the way the Linux test repoints
# /proc/filesystems.
#
# Mock-tested on any host; real AIX df and mount output still need verifying on
# AIX -- in particular that "df -T local" and "/usr/sysv/bin/df -l" are
# accepted, which no stub can prove.
#
# NOT claimed: the remote-df sentinel, which AIX does not have. With
# DF_LOCAL_ONLY=no a wedged remote mount can still hang the client, and nothing
# here bounds it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup aix XYMONCLIENT_AIX

# --- the dialect -------------------------------------------------------------

FSF_LOCAL_TYPE=jfs2;    FSF_LOCAL_MP=/
FSF_PSEUDO_TYPE=procfs; FSF_PSEUDO_MP=/proc
# udfs plays the no-inode-limit role: an optical filesystem the System V df
# cannot give an inode count for, which is what the client's own guard drops.
FSF_NOINODE_TYPE=udfs;  FSF_NOINODE_MP=/dvd
FSF_REMOTE_TYPE=nfs3;   FSF_REMOTE_MP=/mnt
FSF_EXTRA_TYPE=jfs;     FSF_EXTRA_MP=/extra
FSF_DECOY='procf*'

# The inode report has no type filter here; the two contract rules that ask for
# one are skipped, and nothing else is.
FSF_INODE_FILTERED=no

# df -Ik, and the System V df -i behind [inode]. That one is not shaped like
# the disk report at all: the mount point comes FIRST, under a two-word
# "Mount Dir" heading the client seds to Mount_Dir so its awk sees one field.
# Reshuffled by the client into "Filesystem itotal iused avail %iused Mounted
# on", which is where aix.c's "avail", "%iused" and "Mounted" come from.
# (AIX Commands Reference, df: "Mount Dir Filesystem iused avail itotal %iused".)
FSF_DISK_HEADER='Filesystem 1024-blocks Used Free %%Used Mounted on'
FSF_DISK_ROW='/dev/hd4 1048576 319992 728584 31%% %s'
FSF_INODE_HEADER='Mount Dir  Filesystem   iused    avail   itotal  %%iused'
FSF_INODE_ROW='%s /dev/hd4 1504 6688 8192 19%%'
# No inode accounting: itotal 0, which is what the reference shows for /proc
# and what the client's "$5>0" guard drops - field 5 of the seded row is
# itotal, a bare integer, so the comparison is a numeric one.
FSF_INODE_NOLIMIT_ROW='%s /dev/hd4 0 0 0 0'

# There is no exclusion argument: df is asked for everything and the client
# filters what comes back. "-T local" is two arguments, which AIX df requires
# and the single-argument form it rejects; the inode df spells the same thing -l.
FSF_STUB_PARSE='_prev=
for a in "$@"; do
	case "$a" in
		-i) _inode=1 ;;
		-l) _local=1 ;;
		local) [ "$_prev" = "-T" ] && _local=1 ;;
	esac
	_prev=$a
done'

FSF_ARGV_PLAIN='-Ik'
FSF_ARGV_EXCLUDE_PSEUDO=''	# no type filter in df: see fsf_selfcheck

# --- fixtures ----------------------------------------------------------------

# A file whose name the "procf*" token would expand to, if the client let a
# configured type undergo pathname expansion.
: > "$TMP/procfs"

# The rest of the built-in exclude list. The five roles observe only procfs, so
# without these a type dropped from the client's list would go unnoticed until
# it turned up as a permanently-full row on a real host.
FSF_FIXTURE_EXTRA='ahafs /pseudo/ahafs local inode
namefs /pseudo/namefs local inode
autofs /pseudo/autofs local inode
cdrfs /pseudo/cdrfs local inode'

FSF_COMBINED="$TMP/df-section.sh"
fsf_extract "$FSF_COMBINED" "s!/usr/sysv/bin/df!$STUB/df!"
fsf_write_fixture
fsf_write_stub

# mount(8) as AIX prints it, from the same fixture the df stub answers from:
# two header lines, then one row per mount with the vfs type in column 3.
# Every row is written in the local layout with a single device, because the
# shared df stub prints one fixed Filesystem column and the client keys its drop
# on the device+mountpoint pair. The remote layout and two mounts sharing a
# mount point are what AIX alone has to parse, and are exercised further down
# against a fixture built for them.
{
	printf '#!/bin/sh\n'
	printf 'cat <<%sMOUNT%s\n' "'" "'"
	printf '%s\n' '  node       mounted        mounted over    vfs       date        options'
	printf '%s\n' '-------- ---------------  ---------------  ------ ------------ ---------------'
	while read -r _t _mp _rest; do
		[ -n "$_t" ] || continue
		printf '         /dev/hd4         %-16s %-6s Jul 17 08:04 rw\n' "$_mp" "$_t"
	done < "$FSF_FIXTURE"
	printf 'MOUNT\n'
} > "$STUB/mount"
chmod +x "$STUB/mount"

# --- the contract ------------------------------------------------------------

fsf_selfcheck
fsf_contract

# The built-in list, pinned through the report rather than through df's argv:
# every type it names has a filesystem in the fixture, and none may be reported.
assert_not_contains "/pseudo/" "$(fsf_section "$(fsf_report)" df)" \
	"the built-in exclude list still excludes every type it names (procfs ahafs namefs autofs cdrfs)"

# INCLUDE_TYPES names one type, not "stop excluding". The contract asserts only
# that the named type comes back; that a client reading any non-empty list as
# "include everything" would also pass it is not a rule the contract can carry,
# since macOS documents the opposite - there, including the nobrowse attribute
# switches the whole default drop off. So it is asserted here.
out=$(fsf_report "XYMONCLIENT_FS_INCLUDE_TYPES=$FSF_PSEUDO_TYPE")
assert_not_contains "/pseudo/" "$(fsf_section "$out" df)" \
	"INCLUDE_TYPES=$FSF_PSEUDO_TYPE must surface that type alone, not every default exclusion"

# The header is emitted once, ahead of the rows it describes - not repeated per
# surviving row, which is what a filter applied line by line would produce.
assert_equal "1" "$(fsf_section "$(fsf_report)" df | grep -c 'Mounted on')" \
	"the df header must be emitted exactly once, not repeated per surviving row"

# --- AIX's own rules ---------------------------------------------------------

# Everything below runs against a hand-built fixture: two mounts at one mount
# point, a remote mount in mount(8)'s remote layout, and a df stub that logs its
# arguments one per line - "$*" would flatten "-T local" and "-T" "local" into
# the same text, and it is exactly that difference the client has to get right.

cat > "$STUB/df" <<EOF
#!/bin/sh
printf '%s\n' "\$@" >> "$DF_LOG"
_local=no
_prev=
for _a in "\$@"; do
	[ "\$_prev" = "-T" ] && [ "\$_a" = "local" ] && _local=yes
	_prev="\$_a"
done
cat <<'TABLE' | { [ "\$_local" = yes ] && grep -v '^srv:/export ' || cat; }
Filesystem 1024-blocks Used Free %Used Mounted on
/dev/hd4 1048576 319992 728584 31% /
/var/overlay 1048576 319992 728584 42% /data
/dev/hd5 2097152 1048576 1048576 62% /data
srv:/export 2097152 1048576 1048576 50% /mnt
TABLE
EOF
chmod +x "$STUB/df"

# Local rows carry the vfs type in column 3; a remote row puts the node first,
# so column 3 is the slash-starting mount point and the type is column 4.
cat > "$STUB/mount" <<'EOF'
#!/bin/sh
cat <<'TABLE'
  node       mounted        mounted over    vfs       date        options
-------- ---------------  ---------------  ------ ------------ ---------------
         /dev/hd4         /                jfs2   Jul 17 08:04 rw,log=/dev/hd8
         /var/overlay     /data            namefs Jul 17 08:04 rw
         /dev/hd5         /data            jfs2   Jul 17 08:04 rw
 srv      /export         /mnt             nfs3   Jul 17 09:00 rw,bg
TABLE
EOF
chmod +x "$STUB/mount"

# The System V df behind [inode], recording its operands.
SYSVDF="$STUB/sysv-df"
cat > "$SYSVDF" <<EOF
#!/bin/sh
printf '%s\\n' "\$@" >> "$TMP/sysvdf.args"
_l=no
for _a in "\$@"; do [ "\$_a" = "-l" ] && _l=yes; done
cat <<'TABLE' | { [ "\$_l" = yes ] && grep -v '^/mnt ' || cat; }
Mount Dir  Filesystem   iused    avail   itotal  %iused
/          /dev/hd4      1504     6688     8192     19%
/mnt       srv:/export   1504     6688     8192     19%
TABLE
EOF
chmod +x "$SYSVDF"

FSF="$TMP/aix-df-section.sh"
fsf_extract "$FSF" "" '\[inode\]'
FSFI="$TMP/aix-df-inode-section.sh"
fsf_extract "$FSFI" "s!/usr/sysv/bin/df!$SYSVDF!" '\[mount\]'

run_df() {
	( cd "$TMP" && env "$@" $FSF_SHELL "$FSF" ) 2> "$STDERR_LOG" \
		|| fail "extracted [df] block exited non-zero: $(cat "$STDERR_LOG")"
}

run_full() {
	( cd "$TMP" && env "$@" $FSF_SHELL "$FSFI" ) 2> "$STDERR_LOG" \
		|| fail "extracted [df]+[inode] block exited non-zero: $(cat "$STDERR_LOG")"
}

# The namefs overlay on /data goes, the jfs2 filesystem under the same mount
# point stays: the drop is keyed on the device+mountpoint pair, not the path.
out=$(run_df)
assert_not_contains "/var/overlay" "$out" \
	"the namefs overlay row must be dropped by type"
assert_contains "62% /data" "$out" \
	"a real filesystem sharing its mount point with an excluded overlay was dropped with it"

# The remote layout: mount(8) puts the node in its own column, and the device
# renders as node:path in df. Reading that row as a local one would compare the
# wrong pair and leave the mount reported.
out=$(run_df XYMONCLIENT_FS_EXCLUDE_TYPES=nfs3 XYMONCLIENT_FS_DF_LOCAL_ONLY=no)
assert_not_contains " /mnt" "$out" \
	"a remote mount excluded by type must be dropped, so mount's remote row was parsed as node:path"
assert_contains "31% /" "$out" \
	"... and the local filesystems must stay"

# --- DF_LOCAL_ONLY: "-T local", since AIX df has no -l -------------------------

# A hard-mounted NFS server that stops answering wedges df for the life of the
# mount, taking the client run with it. AIX has no remote-df sentinel, so the
# only protection is not asking df about remote mounts at all.

: > "$DF_LOG"
run_df >/dev/null
# One argument per line, so "-T local" as a single argument cannot match: AIX df
# rejects that form, and "$*" logging could not tell the two apart.
assert_match '(^|
)-T
local($|
)' "$(cat "$DF_LOG")" \
	"the default must keep df off remote mounts (-T and local, as two arguments)"

: > "$DF_LOG"
run_df XYMONCLIENT_FS_DF_LOCAL_ONLY=no >/dev/null
assert_not_match 'local' "$(cat "$DF_LOG")" \
	"DF_LOCAL_ONLY=no must let remote filesystems be reported"

: > "$DF_LOG"
run_df XYMONCLIENT_FS_DF_LOCAL_ONLY=maybe >/dev/null
assert_match '(^|
)-T
local($|
)' "$(cat "$DF_LOG")" \
	"an invalid DF_LOCAL_ONLY must fall back to the safe default, not drop the flag"

# --- the inode df is kept off remote mounts too ------------------------------

# Guarding only the disk report would leave the inode df reaching the same dead
# mount. /usr/sysv/bin/df spells local-only as "-l", not "-T local".

: > "$TMP/sysvdf.args"
run_full >/dev/null
args=$(cat "$TMP/sysvdf.args")
assert_contains "-i" "$args" "the inode df must still be asked for inode counts"
assert_match '(^|
)-l($|
)' "$args" \
	"the System V df must be given -l, so the inode report never reaches a remote server"
out=$(run_full)
assert_not_contains " /mnt" "$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')" \
	"a remote mount must not appear in the inode report when df was told local only"

: > "$TMP/sysvdf.args"
run_full XYMONCLIENT_FS_DF_LOCAL_ONLY=no >/dev/null
args=$(cat "$TMP/sysvdf.args")
assert_equal "-i" "$args" \
	"DF_LOCAL_ONLY=no must ask the inode df about every filesystem"

# With every inode row gone the section must be empty, not header-only. Unlike
# the disk report the colour is the same either way - unix_inode_report()
# exempts an empty inode section, for the Solaris host whose filesystems are
# all ZFS - but an empty one has the server name the reason ("No filesystems
# reporting inode data") instead of echoing a header for zero filesystems.
cat > "$SYSVDF" <<EOF
#!/bin/sh
cat <<'TABLE'
Mount Dir  Filesystem   iused    avail   itotal  %iused
TABLE
EOF
chmod +x "$SYSVDF"
out=$(run_full)
inode_sec=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p' | sed 1d)
assert_equal "" "$inode_sec" \
	"an inode report with no rows must be empty, so the server names the reason instead of echoing a header for zero filesystems"

# ... and a df that died is not that case. The server reads an empty inode
# section as green on purpose - it is how a host with nothing inode-limited
# reports - so it cannot tell a dead df from a legitimately empty report. Only
# this side sees the exit status, so only this side can say which it was.
cat > "$SYSVDF" <<'EOF'
#!/bin/sh
echo "df: illegal option -- l" >&2
exit 2
EOF
chmod +x "$SYSVDF"
out=$(run_full)
inode_sec=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p' | sed 1d)
assert_contains "Inode report collection failed: df exited 2 with no output" "$inode_sec" \
	"a failed inode df must name its status, not leave a section the server reads as green"
assert_not_contains "Mounted on" "$inode_sec" \
	"the marker must carry no header - a header with no rows reads as a healthy report"
assert_contains "reporting data as unavailable" "$(cat "$STDERR_LOG")" \
	"a failed inode df must also say so on stderr, as the other clients do"

# --- a df that fails names its status ----------------------------------------

# The contract asserts only that a failed df is loud. What matters here is the
# likeliest cause: a df that rejects "-T local", a flag that needs AIX 7.1. The
# status has to reach the report, so the operator is not left reading
# "incomprehensible disk report" for a flag the host does not have. Kept last:
# it leaves the df stub broken for anything after it.
cat > "$STUB/df" <<'EOF'
#!/bin/sh
echo "df: illegal option -- T" >&2
exit 2
EOF
chmod +x "$STUB/df"
out=$(run_df)
assert_contains "Disk report collection failed: df exited 2 with no output" "$out" \
	"a failed df must name its status, not leave the section empty"
assert_not_contains "Filesystem" "$out" \
	"the failure marker must carry no df header - a header reads as a healthy report"
assert_contains "reporting data as unavailable" "$(cat "$STDERR_LOG")" \
	"a failed df must also say so on stderr, as the other clients do"

# ... and a df that printed rows and still exited non-zero is not that case.
# One unreadable mount is enough to make AIX df complain while it reports the
# rest; if the exclude list then empties the report, what emptied it was the
# exclude list, and saying "no output" about a df that had some is a wrong
# answer to the operator's question.
cat > "$STUB/df" <<'EOF'
#!/bin/sh
cat <<'TABLE'
Filesystem 1024-blocks Used Free %Used Mounted on
/dev/hd4 1048576 319992 728584 31% /
TABLE
echo "df: /badmount: cannot stat" >&2
exit 1
EOF
chmod +x "$STUB/df"
out=$(run_df XYMONCLIENT_FS_EXCLUDE_TYPES=jfs2)
assert_contains "every filesystem excluded" "$out" \
	"rows collected and all of them excluded must be reported as an exclusion, whatever df's status"
assert_not_contains "with no output" "$out" \
	"a df that printed rows must not be reported as having produced none"

pass "xymonclient-aix.sh: the FS filter contract, the mount(8) pair keying, and DF_LOCAL_ONLY in both reports"
