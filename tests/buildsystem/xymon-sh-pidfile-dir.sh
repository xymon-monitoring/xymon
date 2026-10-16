#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/xymon-sh-pidfile-dir.sh
#
# The shipped tasks.cfg puts the daemons' pidfiles under $XYMONRUNDIR, and
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

# The premise: xymond's pidfile really is put there, through PIDFILE or its
# own --pidfile=, since that is the file "reload" signals. Without this the
# rest would pass on a tree where nothing uses the variable at all.
grep -qE -- '(--pidfile=|^[[:space:]]*PIDFILE[[:space:]]+)\$XYMONRUNDIR/xymond\.pid' "$tasks" \
	|| fail "tasks.cfg does not put xymond's pidfile at \$XYMONRUNDIR/xymond.pid, the file xymon.sh reload signals"

# ... and the script must look there, not in the log directory.
if grep -nE '@XYMONLOGDIR@/[^ ]*\.pid|@XYMONLOGDIR@/\*\.pid' "$script"; then
	fail "xymon.sh looks for pidfiles under @XYMONLOGDIR@ while tasks.cfg writes them under \$XYMONRUNDIR"
fi
grep -q '@XYMONRUNDIR@' "$script" \
	|| fail "xymon.sh names no pidfile directory at all"

# And it must create that directory before writing into it. Pointed at /run,
# which is the reason the setting exists, it is gone after a reboot: every
# pidfile write then fails -- xymonlaunch does not report failing to write its
# own -- while "start" still says Xymon started and stop, status and
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

# ---- every task pidfile is there ---------------------------------------------
# Every task pidfile sits with the others in @XYMONRUNDIR@, the one directory
# xymon.sh reads. A SENDHUP task gets its rotation through the launcher either
# way; with its pidfile here, "rotate" also signals it directly, so it gets two.
for d in xymond xymonproxy xymonfetch; do
	grep -qE "^[[:space:]]*PIDFILE[[:space:]]+\\\$XYMONRUNDIR/$d\\.pid" "$tasks" \
		|| fail "tasks.cfg does not give $d the PIDFILE \$XYMONRUNDIR/$d.pid that xymon.sh rotate signals"
done
if grep -nE '(^[[:space:]]*PIDFILE[[:space:]]|--pidfile=)' "$tasks" | grep -v '\$XYMONRUNDIR/'; then
	fail "a task in tasks.cfg writes its pidfile outside \$XYMONRUNDIR, where xymon.sh rotate does not look"
fi

# ---- the client names its own runtime directory ------------------------------
# XYMONRUNDIR is the server's and XYMONCLIENTRUNDIR the client's: a client task
# built from the server's name would put its pidfile wherever the server's
# directory is, on a machine that may have no server at all.
clientcfg="$ROOT/client/clientlaunch.cfg.DIST"
[ -f "$clientcfg" ] || fail "missing shipped file: $clientcfg"

if grep -nE 'XYMONRUNDIR' "$clientcfg"; then
	fail "a client task builds its pidfile from the server's XYMONRUNDIR; use XYMONCLIENTRUNDIR"
fi
grep -q 'XYMONCLIENTRUNDIR' "$clientcfg" \
	|| fail "no client task names XYMONCLIENTRUNDIR, so this contract has nothing to hold"
# and the shipped client configuration defines it, as the client's log
# directory; the built-in default only covers a configuration kept from before.
grep -q '^XYMONCLIENTRUNDIR="\$XYMONCLIENTLOGS"' "$ROOT/client/xymonclient.cfg.DIST" \
	|| fail "client/xymonclient.cfg.DIST does not define XYMONCLIENTRUNDIR as \$XYMONCLIENTLOGS"

# ---- it is settable without patching a file ---------------------------------
# The audience for these settings is the packager, who runs configure
# non-interactively. A variable the script does not read from the environment
# can only be set by editing a shipped file, which a packager must not have
# to do.
conf="$ROOT/configure.server"
[ -f "$conf" ] || fail "missing $conf"
grep -qE '^if test -z "\$XYMONRUNDIR"' "$conf" \
	|| fail "configure.server does not take XYMONRUNDIR from the environment, so a package can only set it by patching a shipped file"
grep -q "XYMONRUNDIR = " "$conf" \
	|| fail "configure.server never writes XYMONRUNDIR into the Makefile, so nothing substitutes it"

# ---- and xymonserver.cfg's placeholders are all substituted -----------------
# Same check as for xymon.sh above, on the other generated file. Adding a
# placeholder without its sed clause leaves a literal @VAR@ in the installed
# configuration, which loadenv() then hands to every program as a path.
# It is generated by one line of the "cfgfiles" target, not by a rule of its
# own, so the line itself is what has to carry every sed clause.
servercfg="$ROOT/xymond/etcfiles/xymonserver.cfg.DIST"
srule=$(grep -F 'etcfiles/xymonserver.cfg.DIST' "$mk")
[ -n "$srule" ] || fail "cannot find the line that generates xymonserver.cfg in xymond/Makefile"

for var in $(grep -oE '@[A-Z_]+@' "$servercfg" | sort -u); do
	case $srule in
		*"$var"*) ;;
		*) fail "xymonserver.cfg.DIST uses $var but the Makefile rule does not substitute it: the installed file would keep it literal and hand it to every program as a path" ;;
	esac
done

# ---- a Makefile that predates the runtime directory -------------------------
# The placeholders are substituted with whatever the make variable holds. A
# top-level Makefile written before XYMONRUNDIR existed does not define it, so
# the substitution yields nothing and every pidfile path collapses to the
# filesystem root -- "/xymond.pid", which as root is created rather than
# refused. Distribution builds always reconfigure; a developer rebuilding in
# place does not. The rules have to default it the way configure would.
require_gnu_make

work=$(mktempdir); register_cleanup "rm -rf '$work'"
probe_rules() { # probe_rules [extra make assignment]
	local extra=${1:-}
	local probe="$work/probe.mk"
	{
		echo "BUILDTOPDIR = $ROOT"
		echo "XYMONLOGDIR = /var/log/xymon"
		echo "XYMONTOPDIR = /usr/lib/xymon"
		[ -n "$extra" ] && echo "$extra"
		echo "include $ROOT/build/Makefile.rules"
		printf 'probe:\n\t@echo "[$(XYMONRUNDIR)]"\n'
	} >"$probe"
	"$XYMON_MAKE" -s -f "$probe" probe 2>/dev/null
}

for case in "undefined:" "empty:XYMONRUNDIR ="; do
	name=${case%%:*}
	extra=${case#*:}
	got=$(probe_rules "$extra") || fail \
		"could not evaluate build/Makefile.rules with XYMONRUNDIR $name, so this
check cannot judge it"
	[ "$got" = "[/var/log/xymon]" ] || fail \
		"with XYMONRUNDIR $name in the top-level Makefile, the rules gave $got
rather than the log directory. xymon.sh has the value built in, so its pidfile
would be /xymonlaunch.pid at the filesystem root. Note that ?= does not fix this -- it assigns only when a
variable is undefined, not when it is defined empty."
done

pass "xymon.sh, tasks.cfg, configure.server and the Makefiles agree on XYMONRUNDIR, and the client uses its own"
