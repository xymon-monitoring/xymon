#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/xymonclient-cfg-dirs.sh
#
# xymonclient.cfg names where the client writes its own files, XYMONTMP and
# XYMONCLIENTLOGS, by reference: "$XYMONCLIENTTMPDIR" and
# "$XYMONCLIENTLOGDIR", which the programs resolve from the directories they
# were built for. A build that relocated them (INSTALLCLIENTTMPDIR,
# INSTALLCLIENTLOGDIR, or in a client-only build INSTALLTMPDIR) resolves to
# those; a default build to $XYMONHOME/tmp and $XYMONHOME/logs, so a client's
# directory still moves as a whole.
#
# One shipped file serves any build: the programs resolve the two references
# from the directories they were built for, so the file carries no path and
# needs no edit after install (#1050 installs it whole, overwriting the old
# copy to .bak). Pinned here:
#   - the shipped file holds the two references;
#   - lib/environ.c, compiled as a default build and as two relocated ones,
#     resolves one and the same file to each build's directories, lets a
#     value set in the file or in its xymonclient.d win, and still resolves
#     a file with the old lines;
#   - the build hands lib/ the relocated directories and nothing for a
#     default one, and lib/ notes them so that a change rebuilds environ.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")
dist="$ROOT/client/xymonclient.cfg.DIST"
for f in "$dist" "$ROOT/lib/Makefile" "$ROOT/lib/environ.c"; do
	[ -f "$f" ] || fail "missing shipped file: $f"
done
require_gnu_make

work=$(mktempdir)

# --- the shipped file ---------------------------------------------------

two() { grep -E '^(XYMONTMP|XYMONCLIENTLOGS)=' "$1" | sed 's/[[:space:]]*#.*//'; }

shipped=$(two "$dist")
assert_contains 'XYMONTMP="$XYMONCLIENTTMPDIR"' "$shipped" \
	"xymonclient.cfg.DIST does not name XYMONTMP by reference"
assert_contains 'XYMONCLIENTLOGS="$XYMONCLIENTLOGDIR"' "$shipped" \
	"xymonclient.cfg.DIST does not name XYMONCLIENTLOGS by reference"

# --- what the programs resolve --------------------------------------------

if [ -f "$ROOT/include/config.h" ] && [ -f "$(xymon_lib "$ROOT" libxymonclient.a)" ]; then
	require_c_buildenv "$ROOT"
	build_xymon_libs "$ROOT" "$work/libbuild.log" libxymonclient.a
	harness_cflags=$(xymon_cflags "$ROOT")
	harness_ldflags=$(xymon_ldflags "$ROOT")

	# harness NAME [-DXYMONCLIENTTMPDIR=... -DXYMONCLIENTLOGDIR=...] -- a
	# program with the environ.c of a build that relocated the client's
	# directories that way, or of a default build
	harness() {
		local name=$1; shift
		# shellcheck disable=SC2086 # the flag strings are intentionally word-split
		"$CC" $harness_cflags "$@" \
			-DXYMONBUILD='""' -DXYMONTOPDIR='"/t"' -DXYMONLOGDIR='"/t/log"' -DXYMONHOSTNAME='"h"' \
			-DXYMONHOSTIP='"127.0.0.1"' -DXYMONHOSTOS='"linux"' -DXYMONHOME='"/t/client"' \
			-o "$work/$name" "$here/xymonclient-cfg-dirs-harness.c" "$ROOT/lib/environ.c" \
			"$(xymon_lib "$ROOT" libxymonclient.a)" $harness_ldflags 2>"$work/cc.log" \
			|| fail "the harness for a $name build does not compile: $(cat "$work/cc.log")"
	}
	harness default
	harness moved-a -DXYMONCLIENTTMPDIR='"/var/tmp/a"' -DXYMONCLIENTLOGDIR='"/var/log/a"'
	harness moved-b -DXYMONCLIENTTMPDIR='"/var/tmp/b"' -DXYMONCLIENTLOGDIR='"/var/log/b"'

	# sees BUILD HOME FILE -- XYMONTMP|XYMONCLIENTLOGS as a program of that
	# build finds them, started with the client's home at HOME
	sees() { env -i PATH=/usr/bin:/bin XYMONCLIENTHOME="$2" "$work/$1" "$3" 2>&1; }

	# The shipped lines, after the one that sets XYMONHOME, as in the file.
	{ grep '^XYMONHOME=' "$dist"; grep -E '^(XYMONTMP|XYMONCLIENTLOGS)=' "$dist"; } >"$work/shipped.cfg"
	[ "$(wc -l <"$work/shipped.cfg" | tr -d ' ')" = 3 ] \
		|| fail "xymonclient.cfg.DIST does not hold one XYMONHOME, XYMONTMP and XYMONCLIENTLOGS line each"

	assert_equal "/opt/x/tmp|/opt/x/logs" "$(sees default /opt/x "$work/shipped.cfg")" \
		"a default build does not resolve the client's directories under its home"
	assert_equal "/moved/x/tmp|/moved/x/logs" "$(sees default /moved/x "$work/shipped.cfg")" \
		"a default build's directories do not follow the client's home when it is moved"

	# One file, never rewritten; three builds. The second relocation is the
	# case a path written into the file gets wrong.
	assert_equal "/var/tmp/a|/var/log/a" "$(sees moved-a /opt/x "$work/shipped.cfg")" \
		"a build that relocated the client's directories does not resolve them"
	assert_equal "/var/tmp/b|/var/log/b" "$(sees moved-b /opt/x "$work/shipped.cfg")" \
		"a build that relocated the client's directories a second time does not resolve the new ones from the same file"

	# A value set in the file wins over the build's.
	{ grep '^XYMONHOME=' "$dist"; echo 'XYMONTMP="/srv/xymon/tmp"'; grep '^XYMONCLIENTLOGS=' "$dist"; } >"$work/own-tmp.cfg"
	assert_equal "/srv/xymon/tmp|/var/log/b" "$(sees moved-b /opt/x "$work/own-tmp.cfg")" \
		"a directory set in xymonclient.cfg does not win over the one the build relocated"

	# So does one set in the xymonclient.d drop-in directory, which the file
	# includes after its own settings: where an administrator's change goes,
	# since install never writes there.
	mkdir -p "$work/xymonclient.d"
	echo 'XYMONTMP="/srv/xymon/tmp"' >"$work/xymonclient.d/local.cfg"
	{ cat "$work/shipped.cfg"; echo "optional directory $work/xymonclient.d"; } >"$work/dropin.cfg"
	assert_equal "/srv/xymon/tmp|/var/log/b" "$(sees moved-b /opt/x "$work/dropin.cfg")" \
		"a directory set in xymonclient.d does not win over the one the build relocated"

	# A file install has not touched yet keeps working: through the links
	# install leaves at the old places.
	printf '%s\n' 'XYMONHOME="$XYMONCLIENTHOME"' 'XYMONTMP="$XYMONHOME/tmp"' 'XYMONCLIENTLOGS="$XYMONHOME/logs"' >"$work/old.cfg"
	assert_equal "/opt/x/tmp|/opt/x/logs" "$(sees moved-b /opt/x "$work/old.cfg")" \
		"a file with the lines shipped until now no longer resolves to the directories under the client's home"
	resolved=yes
else
	resolved=no
fi

# --- the build hands the directories to lib/, which notes them -------------

# lib/ rewrites clientdirs.def only when the directories change: environ
# depends on it, and must not be rebuilt by every make.
mkdir -p "$work/tree/lib"
cp "$ROOT/lib/Makefile" "$work/tree/lib/"
def="$work/tree/lib/clientdirs.def"
note() {
	"$XYMON_MAKE" -s -C "$work/tree/lib" "$@" clientdirs.def >"$work/note.log" 2>&1 \
		|| fail "lib/Makefile did not note the client's directories: $(cat "$work/note.log")"
}
note
assert_equal "|" "$(cat "$def")" "a default build does not note empty client directories in lib/clientdirs.def"
note XYMONCLIENTTMPDIRDEF=/var/tmp/a XYMONCLIENTLOGDIRDEF=/var/log/a
assert_equal "/var/tmp/a|/var/log/a" "$(cat "$def")" "lib/clientdirs.def does not hold the relocated directories"
: >"$work/marker"
# A filesystem with one-second timestamps would call a file rewritten in the
# same second not newer, and hide a rewrite.
sleep 1
note XYMONCLIENTTMPDIRDEF=/var/tmp/a XYMONCLIENTLOGDIRDEF=/var/log/a
if [ "$def" -nt "$work/marker" ]; then
	fail "lib/clientdirs.def was rewritten although the directories did not change: environ would be rebuilt by every make"
fi
note XYMONCLIENTTMPDIRDEF=/var/tmp/b XYMONCLIENTLOGDIRDEF=/var/log/a
assert_equal "/var/tmp/b|/var/log/a" "$(cat "$def")" "lib/clientdirs.def kept the directory of an earlier build"

# The values the build works out, by dry runs of the target that builds lib/
# on a configured tree. The default is nothing, whatever the trailing slashes,
# so that the programs keep the relocatable default. CLIENTONLY is given each
# time: the tree may be configured either way, and a client-only build's
# default tmp directory is its INSTALLTMPDIR, not one under the client's home.
if [ -f "$ROOT/Makefile" ]; then
	handed() {  # handed [VAR=VALUE...] -- the two values on the line that builds lib/
		local out
		out=$("$XYMON_MAKE" -B -n -C "$ROOT" "$@" -o include/config.h lib-build 2>&1) || true
		sed -n '/XYMONCLIENTTMPDIRDEF=/{s/.*\(XYMONCLIENTTMPDIRDEF="[^"]*"\) \(XYMONCLIENTLOGDIRDEF="[^"]*"\).*/\1 \2/p;q;}' <<<"$out"
	}
	assert_equal 'XYMONCLIENTTMPDIRDEF="" XYMONCLIENTLOGDIRDEF=""' \
		"$(handed CLIENTONLY= XYMONCLIENTHOME=/opt/x INSTALLCLIENTTMPDIR= INSTALLCLIENTLOGDIR=)" \
		"a default build hands lib/ a client directory, so its programs would not keep the relocatable default"
	assert_equal 'XYMONCLIENTTMPDIRDEF="" XYMONCLIENTLOGDIRDEF=""' \
		"$(handed CLIENTONLY= XYMONCLIENTHOME=/opt/x/ INSTALLCLIENTTMPDIR=/opt/x/tmp/ INSTALLCLIENTLOGDIR=/opt/x/logs)" \
		"the default directories, spelled with a trailing slash, are handed to lib/ as relocated ones"
	assert_equal 'XYMONCLIENTTMPDIRDEF="" XYMONCLIENTLOGDIRDEF=""' \
		"$(handed CLIENTONLY= XYMONCLIENTHOME=/opt//x INSTALLCLIENTTMPDIR=/opt/x//tmp INSTALLCLIENTLOGDIR=/opt//x/logs/)" \
		"the default directories, spelled with doubled slashes, are handed to lib/ as relocated ones"
	assert_equal 'XYMONCLIENTTMPDIRDEF="/var/tmp/handed" XYMONCLIENTLOGDIRDEF="/var/log/handed"' \
		"$(handed CLIENTONLY= XYMONCLIENTHOME=/opt/x INSTALLCLIENTTMPDIR=/var//tmp/handed INSTALLCLIENTLOGDIR=/var/log/handed//)" \
		"relocated client directories keep a doubled or trailing slash when handed to lib/"
	assert_equal 'XYMONCLIENTTMPDIRDEF="/var/tmp/handed" XYMONCLIENTLOGDIRDEF="/var/log/handed"' \
		"$(handed CLIENTONLY= XYMONCLIENTHOME=/opt/x INSTALLCLIENTTMPDIR=/var/tmp/handed/ INSTALLCLIENTLOGDIR=/var/log/handed)" \
		"relocated client directories do not reach lib/, or keep a trailing slash"
	assert_equal 'XYMONCLIENTTMPDIRDEF="/var/tmp/only" XYMONCLIENTLOGDIRDEF=""' \
		"$(handed CLIENTONLY=yes XYMONCLIENTHOME=/opt/x INSTALLTMPDIR=/var/tmp/only INSTALLCLIENTTMPDIR= INSTALLCLIENTLOGDIR=)" \
		"a client-only build's INSTALLTMPDIR does not reach lib/ as the client's tmp directory"
	handed_down=yes
else
	handed_down=no
fi

what="xymonclient.cfg names the client's tmp and log directories by reference, so one shipped file serves any build, and lib/ notes the directories without rebuilding environ for nothing"
if [ "$resolved" = yes ] && [ "$handed_down" = yes ]; then
	pass "$what; one file resolves to each build's directories, a second relocation included, and follows a moved home by default; the build hands lib/ the relocated directories only"
elif [ "$resolved" = yes ]; then
	pass_partial "$what; one file resolves to each build's directories" "the hand-down from the top-level build needs a configured tree"
else
	pass_partial "$what" "what the programs resolve needs a built tree, and the hand-down a configured one"
fi
