#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/xymon-sh-pidfile-dir.sh
#
# The shipped tasks.cfg starts the daemons with --pidfile=$XYMONRUNDIR/..., and
# xymon.sh -- the script an operator actually runs -- looks those pidfiles up
# again for "reload" and "rotate". They have to name the same directory. When
# XYMONRUNDIR was introduced only the first half moved: with the default the two
# coincide and nothing shows, but the moment XYMONRUNDIR is pointed anywhere
# else -- /run/xymon, which is the reason it exists -- "reload" answered "xymond
# not running" and "rotate" signalled nobody.
#
# Checked on the shipped files rather than by running the script, which would
# need a live xymond and its environment. What can be checked cheaply is that
# the two ends agree, and that every placeholder the script uses is one the
# Makefile actually substitutes: an unsubstituted @XYMONRUNDIR@ would be worse
# than the wrong directory.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
script="$ROOT/xymond/xymon.sh.DIST"
tasks="$ROOT/xymond/etcfiles/tasks.cfg.DIST"
mk="$ROOT/xymond/Makefile"

for f in "$script" "$tasks" "$mk"; do
	[ -f "$f" ] || fail "missing shipped file: $f"
done

# The premise: the tasks really are told to write there. Without this the rest
# would pass on a tree where nothing uses the variable at all.
grep -q -- '--pidfile=\$XYMONRUNDIR/' "$tasks" \
	|| fail "no task writes its pidfile under \$XYMONRUNDIR, so this contract has nothing to hold"

# ... and the script must look there, not in the log directory.
if grep -nE '@XYMONLOGDIR@/[^ ]*\.pid|@XYMONLOGDIR@/\*\.pid' "$script"; then
	fail "xymon.sh looks for pidfiles under @XYMONLOGDIR@ while tasks.cfg writes them under \$XYMONRUNDIR"
fi
grep -q '@XYMONRUNDIR@' "$script" \
	|| fail "xymon.sh names no pidfile directory at all"

# And it must create that directory before writing into it. Pointed at /run,
# which is the reason the setting exists, it is gone after a reboot: every
# pidfile write then fails silently -- xymonlaunch does not report a pidfile it
# could not open -- while "start" still says Xymon started and stop, status and
# reload have nothing left to read.
grep -qE 'mkdir( -p)? @XYMONRUNDIR@' "$script" \
	|| fail "xymon.sh writes pidfiles into @XYMONRUNDIR@ without creating it: a volatile /run leaves start reporting success with no pidfile"

# Every placeholder it uses must be substituted when the script is generated,
# or the installed copy keeps the literal @VAR@.
rule=$(sed -n '/^xymon\.sh: xymon\.sh\.DIST/,/^$/p' "$mk")
[ -n "$rule" ] || fail "cannot find the xymon.sh rule in xymond/Makefile"

for var in $(grep -oE '@[A-Z_]+@' "$script" | sort -u); do
	case $rule in
		*"$var"*) ;;
		*) fail "xymon.sh uses $var but the Makefile rule does not substitute it: the installed script would keep it literal" ;;
	esac
done

# ---- each side names its own runtime directory ------------------------------
# XYMONRUNDIR is the server's and XYMONCLIENTRUNDIR the client's, the way
# XYMONSERVERLOGS and XYMONCLIENTLOGS already split. It matters beyond tidiness:
# xymonlaunch expands a task's pidfile path in the parent, which holds one value
# per name -- so a client path written with the server's name is expanded with
# the server's value, and the launcher removes a file it never wrote. Two names
# make every path unambiguous whichever side reads it.
clientcfg="$ROOT/client/clientlaunch.cfg.DIST"
[ -f "$clientcfg" ] || fail "missing shipped file: $clientcfg"

if grep -nE 'XYMONRUNDIR' "$clientcfg"; then
	fail "a client task builds its pidfile from the server's XYMONRUNDIR; use XYMONCLIENTRUNDIR"
fi
grep -q 'XYMONCLIENTRUNDIR' "$clientcfg" \
	|| fail "no client task names XYMONCLIENTRUNDIR, so this contract has nothing to hold"

# The server must be able to expand both, since its launcher starts the client.
grep -q '^XYMONCLIENTRUNDIR=' "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" \
	|| fail "xymonserver.cfg does not define XYMONCLIENTRUNDIR: a server-side launcher cannot expand a client task's pidfile path"

pass "xymon.sh looks for pidfiles where tasks.cfg writes them, each side names its own runtime directory, and every placeholder is substituted"
