#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-safe.sh
#
# build/install-safe is the install primitive: as root it installs a file, or
# sets the owner and mode of a name, into a directory owned by the unprivileged
# Xymon user, without following a symlink at the destination name. These checks
# compile it and exercise the symlink and atomicity behaviour that make it safe;
# they run as any user (the owner-change path needs root and is checked only for
# its no-follow property, which does not).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/build/install-safe.c" ] || skip "build/install-safe.c not present"

work=$(mktempdir)
bin=$work/install-safe
# Compile with the tree's own flags, like the other compiled test helpers.
${CC:-cc} ${CFLAGS:-} -o "$bin" "$ROOT/build/install-safe.c" \
	|| fail "install-safe.c did not compile"

# Read a name's permission bits as the ls(1) string (stat -c is not portable).
perms() { ls -ld "$1" | cut -c1-10; }

cd "$work"
printf 'the-source\n' > src

# 1. A plain install creates the file with the requested mode.
"$bin" copy -m 0644 src a
[ -f a ] && [ "$(cat a)" = the-source ] || fail "install did not place the file content"
[ "$(perms a)" = -rw-r--r-- ] || fail "install did not set mode 0644 (got $(perms a))"

# 2. Installing over a symlink replaces the name; it does not write through it.
printf 'victim-unchanged\n' > victim
ln -sf "$work/victim" b
"$bin" copy -m 0644 src b
[ "$(cat victim)" = victim-unchanged ] || fail "install wrote through a symlink to its target"
[ ! -L b ] || fail "install left the name a symlink instead of the file"
[ "$(cat b)" = the-source ] || fail "install did not replace the symlink with the file"

# 3. Setting the mode of a symlink does not follow it to the target.
printf 'keep\n' > real; chmod 600 real
ln -sf "$work/real" c
"$bin" set -m 0644 c
[ "$(perms real)" = -rw------- ] || fail "set-mode followed a symlink and changed its target mode (got $(perms real))"

# 4. Setting the mode of a real file applies.
printf 'x\n' > d
"$bin" set -m 0640 d
[ "$(perms d)" = -rw-r----- ] || fail "set-mode did not apply to a real file (got $(perms d))"

# 5. A directory is created and its mode set.
"$bin" mkdir -m 0755 e
[ -d e ] && [ "$(perms e)" = drwxr-xr-x ] || fail "directory was not created with mode 0755 (got $(perms e))"

# 5b. mkdir creates missing parent directories, and follows an administrator's
#     relocation symlink in the parent path (as mkdir -p does) to create under
#     its target. The final name is what mkdir must not follow (case 10).
"$bin" mkdir -m 0755 pp/qq/rr
[ -d pp/qq/rr ] || fail "mkdir did not create missing parent directories"
mkdir preal; ln -sf "$work/preal" plink
"$bin" mkdir -m 0755 plink/sub || fail "mkdir did not follow a relocation symlink in the parent path"
[ -d preal/sub ] || fail "mkdir did not create the directory under the relocated parent"

# 6. The private temporary name is unpredictable: a symlink planted at the old
#    "<dest>.inst.<pid>" name is neither used nor followed. The install still
#    succeeds, the planted symlink is left in place (proving the real temp was a
#    different, random name), and its target is untouched.
printf 'temp-victim\n' > tvictim
ln -sf "$work/tvictim" "f.inst.$$" 2>/dev/null || true
"$bin" copy -m 0644 src f || fail "copy failed with a symlink planted at the predictable temp name"
[ "$(cat f)" = the-source ] || fail "the install did not produce the real file at the name"
[ "$(cat tvictim)" = temp-victim ] || fail "the install followed the predictable temporary name to its target"
[ -L "f.inst.$$" ] || fail "the install consumed the predictable temp name instead of an unpredictable one"

# 7. A name hard-linked to another file is not chowned/chmod'd (no acting on a
#    file the install does not own outright).
printf 'other\n' > other; ln other hl 2>/dev/null && {
	"$bin" set -m 0640 hl || true
	[ "$(perms other)" = -rw-r--r-- ] || fail "set-mode changed a hard-linked file's target mode"
}

# 8. A file install with no -m is refused rather than leaving a 0600 file.
"$bin" copy src nomode 2>/dev/null && fail "file install without -m should fail"
[ ! -e nomode ] || fail "a refused install left a file behind"

# 9. A bad mode is rejected.
"$bin" copy -m 8abc src badmode 2>/dev/null && fail "a bad octal mode should be rejected"

# 10. -d on a symlink to a directory leaves the target's mode alone.
mkdir realdir; chmod 700 realdir
ln -sf "$work/realdir" dlink
"$bin" mkdir -m 0755 dlink
[ "$(perms realdir)" = drwx------ ] || fail "-d followed a symlink and changed the target directory's mode (got $(perms realdir))"

# 11. -d on a dangling symlink is a failure, not a silent success.
ln -sf "$work/nowhere" dangle
"$bin" mkdir -m 0755 dangle 2>/dev/null && fail "-d on a dangling symlink should fail"
[ ! -e "$work/nowhere" ] || fail "-d created the symlink's target"

# 12. -c with no -m clears a setuid bit it would otherwise leave on a name.
printf 'z\n' > suid; chmod 4755 suid
"$bin" set -o "$(id -un)" suid
case "$(perms suid)" in *s*) fail "-c left the setuid bit set";; esac

# 13. A trailing slash is refused (it would resolve through a symlink).
"$bin" set -m 0644 "d/" 2>/dev/null && fail "a trailing-slash name should be refused"

# 13b. A setuid/setgid copy owned by a non-root user is refused: that user would
#      own the finished inode and could rewrite its content before the mode is
#      set. Owner 1 is non-root whether or not the test itself runs as root.
"$bin" copy -m 4755 -o 1 src suidbad 2>/dev/null && fail "setid copy to a non-root owner should be refused"
[ ! -e suidbad ] || fail "a refused setid copy left a file behind"

# 13b'. A setuid/setgid copy that is group- or world-writable is refused even
#       when root-owned: anyone in the group could rewrite the privileged file.
"$bin" copy -m 6775 -o 0 src suidw 2>/dev/null && fail "group-writable setid copy should be refused"
[ ! -e suidw ] || fail "a refused writable setid copy left a file behind"

# 13c. A file installed by root takes root's group, not the directory's: the
#      install must not let the unprivileged user's group leak onto it. Only
#      meaningful as root (chgrp needs it, and the force acts on the egid). The
#      directory is setgid with a non-root group, so a new file would otherwise
#      inherit that group -- always on BSD/macOS, and under setgid on Linux.
if [ "$(id -u)" = 0 ]; then
	mkdir ggdir && chgrp 1 ggdir && chmod 2755 ggdir
	"$bin" copy -m 0644 src ggdir/gf
	fg=$(ls -ld ggdir/gf | awk '{print $4}')
	dg=$(ls -ld ggdir    | awk '{print $4}')
	[ "$fg" != "$dg" ] || fail "installed file kept the directory's group ($fg) instead of root's"
fi

# 14. touch creates an absent file, preserves a present one, and leaves a symlink.
"$bin" touch -m 0664 newf
[ -f newf ] && [ ! -s newf ] && [ "$(perms newf)" = -rw-rw-r-- ] || fail "touch did not create an empty file with the mode"
# An existing plain file keeps its content and is given the requested mode --
# the same path a name that races in after the initial lstat is routed through.
printf 'admin\n' > keepf; chmod 600 keepf
"$bin" touch -m 0664 keepf
[ "$(cat keepf)" = admin ] || fail "touch truncated an existing file"
[ "$(perms keepf)" = -rw-rw-r-- ] || fail "touch did not set the mode of an existing file (got $(perms keepf))"
printf 'persisted\n' > store; ln -sf "$work/store" tlink
"$bin" touch -m 0664 tlink
[ -L tlink ] && [ "$(cat store)" = persisted ] || fail "touch followed or removed a symlink"

pass "install-safe installs atomically, never follows a symlink at a destination name, refuses a non-plain file, and does not leave setuid on a name set in place"
