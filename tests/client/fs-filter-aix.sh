#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-aix.sh
#
# xymonclient-aix.sh under the type-filter subset of the XYMONCLIENT_FS_*
# contract. AIX df has no type exclusion, so the client's dialect is unique:
# it reads the vfs column of mount(8) and drops excluded types from df's
# output by mount point. Pinned here, against a stub df and an AIX-format
# stub mount:
#
#   - the default set (procfs ahafs namefs autofs cdrfs) is dropped from the
#     [df] section; real local (jfs2) and remote (nfs3) rows stay
#   - the drop is keyed on the device+mountpoint pair, so an excluded overlay
#     (namefs) sharing its mount point with a real filesystem takes only the
#     overlay row with it
#   - excluding every filesystem yields an EMPTY [df] section, which the
#     server flags - a header-only section would read as all-green
#   - INCLUDE_TYPES surfaces a default-excluded type again
#   - EXCLUDE_TYPES drops a real type; a type in both lists stays excluded
#   - a configured token like "procf*" is matched literally, never expanded
#     against files in the working directory
#
# NOT claimed: the full #170 contract. DF_LOCAL_ONLY and the remote-df
# sentinel are not implemented on AIX, and the [inode] section is untouched
# (its own guard already skips rows without inode accounting, and its
# /usr/sysv/bin/df is an absolute path a PATH stub cannot reach).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup aix XYMONCLIENT_AIX

# --- stubs --------------------------------------------------------------------

# AIX df -Ik: pseudo mounts print dashes, real mounts print numbers. One row
# per default-excluded type, so a type dropped from the client's list shows up
# as a reappearing row.
cat > "$STUB/df" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> "$DF_LOG"
cat <<'TABLE'
Filesystem 1024-blocks Used Free %Used Mounted on
/dev/hd4 1048576 319992 728584 31% /
/proc - - - - /proc
/aha - - - - /aha
/dev/cd0 693248 693248 0 100% /cdrom
/var/somefile 1048576 319992 862080 17% /namefs
/dev/autofs - - - - /autodir
/var/overlay 1048576 319992 728584 42% /data
/dev/hd5 2097152 1048576 1048576 62% /data
srv:/export 2097152 1048576 1048576 50% /mnt
TABLE
EOF
chmod +x "$STUB/df"

# AIX mount: two header lines; local rows have the vfs type in column 3,
# remote rows carry the node first and the type in column 4.
cat > "$STUB/mount" <<'EOF'
#!/bin/sh
cat <<'TABLE'
  node       mounted        mounted over    vfs       date        options
-------- ---------------  ---------------  ------ ------------ ---------------
         /dev/hd4         /                jfs2   Jul 17 08:04 rw,log=/dev/hd8
         /proc            /proc            procfs Jul 17 08:04 rw
         /aha             /aha             ahafs  Jul 17 08:04 rw
         /dev/cd0         /cdrom           cdrfs  Jul 17 08:04 ro
         /var/somefile    /namefs          namefs Jul 17 08:04 rw
         /dev/autofs      /autodir         autofs Jul 17 08:04 rw
         /var/overlay     /data            namefs Jul 17 08:04 rw
         /dev/hd5         /data            jfs2   Jul 17 08:04 rw
 srv      /export         /mnt             nfs3   Jul 17 09:00 rw,bg
TABLE
EOF
chmod +x "$STUB/mount"

# Files the "procf*" / "jfs*" tokens would expand to, if the client let a
# configured type undergo pathname expansion in the directory it runs from.
: > "$TMP/procfs"
: > "$TMP/jfs2"

FSF="$TMP/df-section.sh"
fsf_extract "$FSF" "" '\[inode\]'

run_df() {
	( cd "$TMP" && env "$@" $FSF_SHELL "$FSF" ) 2> "$STDERR_LOG" \
		|| fail "extracted [df] block exited non-zero: $(cat "$STDERR_LOG")"
}

# --- the default set ----------------------------------------------------------

out=$(run_df)
assert_contains "%Used Mounted on" "$out" "the df header row must survive the filter"
assert_contains "31% /" "$out" "the real jfs2 filesystem must be reported"
assert_contains "50% /mnt" "$out" "a remote nfs3 mount is not in the type exclude set and must be reported"
for mp in /proc /aha /cdrom /namefs /autodir; do
	assert_not_contains " $mp" "$out" \
		"the default exclude set no longer drops $mp (procfs ahafs namefs autofs cdrfs)"
done

# The namefs overlay on /data goes, the jfs2 filesystem under the same mount
# point stays: the drop is keyed on the device+mountpoint pair, not the path.
assert_not_contains "/var/overlay" "$out" \
	"the namefs overlay row must be dropped by type"
assert_contains "62% /data" "$out" \
	"a real filesystem sharing its mount point with an excluded overlay was dropped with it"

# --- INCLUDE_TYPES surfaces a default exclusion --------------------------------

out=$(run_df XYMONCLIENT_FS_INCLUDE_TYPES=procfs)
assert_contains " /proc" "$out" "INCLUDE_TYPES=procfs must surface /proc again"
assert_not_contains " /aha" "$out" "INCLUDE_TYPES=procfs must not surface other defaults"

# --- EXCLUDE_TYPES drops a real type, and wins over INCLUDE --------------------

out=$(run_df XYMONCLIENT_FS_EXCLUDE_TYPES=jfs2)
assert_not_contains "31% /" "$out" "EXCLUDE_TYPES=jfs2 must drop the jfs2 filesystem"
assert_contains "50% /mnt" "$out" "EXCLUDE_TYPES=jfs2 must not drop unrelated mounts"

out=$(run_df XYMONCLIENT_FS_INCLUDE_TYPES=procfs XYMONCLIENT_FS_EXCLUDE_TYPES=procfs)
assert_not_contains " /proc" "$out" "a type in both lists must stay excluded (EXCLUDE wins)"

# Multiple tokens in one variable are independent list entries.
out=$(run_df XYMONCLIENT_FS_INCLUDE_TYPES='procfs ahafs')
assert_contains " /proc" "$out" "the first of two INCLUDE_TYPES tokens must surface its type"
assert_contains " /aha" "$out" "the second of two INCLUDE_TYPES tokens must surface its type"

out=$(run_df XYMONCLIENT_FS_EXCLUDE_TYPES='jfs2 nfs3')
assert_not_contains "31% /" "$out" "the first of two EXCLUDE_TYPES tokens must drop its type"
assert_not_contains " /mnt" "$out" "the second of two EXCLUDE_TYPES tokens must drop its type"
# ... which excludes every filesystem in the fixture: the section must come
# out empty, not header-only - the server flags an empty disk report, while a
# header with no rows reads as all-green.
assert_equal "[df]" "$out" \
	"excluding every filesystem left a header-only [df] section, which the server reads as all-green"

# --- configured tokens are literal, never globs --------------------------------

# "procf*" must not expand to the "procfs" file in the working directory: as a
# literal it matches no type, so /proc stays excluded.
out=$(run_df XYMONCLIENT_FS_INCLUDE_TYPES='procf*')
assert_not_contains " /proc" "$out" \
	"a configured type underwent pathname expansion (procf* matched a file named procfs)"

# Same on the exclude side, where the token travels through the computed list
# itself: literal "jfs*" matches no type, so the jfs2 filesystem stays.
out=$(run_df XYMONCLIENT_FS_EXCLUDE_TYPES='jfs*')
assert_contains "31% /" "$out" \
	"a configured exclude underwent pathname expansion (jfs* matched a file named jfs2)"

pass "xymonclient-aix.sh: type filtering via mount(8), since AIX df cannot exclude by type"
