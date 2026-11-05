#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonreports-gnudate.sh
#
# xymonreports.sh needs GNU date, and finds it instead of being edited:
# GNUDATE from the environment wins, which xymonserver.cfg can set for the
# periodic tasks, and is checked the same way, so a wrong line there stops
# the script too; left unset, the script takes the first of gdate and date
# on PATH whose --version says GNU coreutils; with neither it stops with
# one line on stderr and does not run xymongen. The installed script is
# regenerated from xymonreports.sh.DIST by every make install, so an edited
# GNUDATE= line there was lost at the next upgrade, and a BSD date ran the
# reports into a directory named by its error output.
#
# The script is built from its .DIST here as xymond/Makefile builds it, and
# run against stand-ins: a GNU date that answers --version and the formats
# the script asks for with fixed values, a BSD date that rejects every long
# option, and an xymongen that only records its arguments. The PATH it runs
# on holds the stand-ins and the two tools the script itself needs, and no
# system directory: Solaris 11.4 has a real GNU date at /usr/bin/gdate, which
# the script rightly prefers to a date on PATH, so a PATH that kept /usr/bin
# made the cases without gdate find one anyway.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_cfg REPORTS_DIST "xymond/xymonreports.sh.DIST"

work=$(mktempdir)
home=$work/home
mkdir -p "$home/bin" "$home/www" "$work/gnu" "$work/bsd" "$work/both" "$work/tools"

# What the script runs besides date and xymongen, found now and linked into
# the only other directory on its PATH.
for tool in grep mkdir; do
	ln -s "$(command -v "$tool")" "$work/tools/$tool"
done

sed -e "s!@XYMONHOME@!$home!g" "$REPORTS_DIST" >"$work/xymonreports.sh"
chmod 755 "$work/xymonreports.sh"

# A GNU date: --version says coreutils, and each format the script asks for
# gets a fixed answer, so the report window is predictable.
cat >"$work/gnudate" <<'GNU'
#!/bin/sh
echo "$0 $*" >>"$GNUDATE_LOG"
case "$*" in
	--version)        echo "date (GNU coreutils) 9.4" ;;
	*"+%Y/%m/%d")     echo "2026/11/03" ;;
	*"+%Y/%V")        echo "2026/44" ;;
	*"+%Y/%m")        echo "2026/11" ;;
	*"+%d %b %Y")     echo "03 Nov 2026" ;;
	"+%s --date 03 Nov 2026 00:00:00") echo 1762124400 ;;
	"+%s --date 03 Nov 2026 23:59:59") echo 1762210799 ;;
	*)                echo "unexpected date arguments: $*" >&2; exit 2 ;;
esac
GNU
# A BSD date: no long options at all, as on the BSDs and macOS.
cat >"$work/bsddate" <<'BSD'
#!/bin/sh
case "$1" in
	--*|+%s) echo "date: illegal option -- -" >&2; exit 1 ;;
esac
exec /bin/date "$@"
BSD
cat >"$home/bin/xymongen" <<'GEN'
#!/bin/sh
echo "$*" >"$XYMONGEN_LOG"
GEN
chmod 755 "$work/gnudate" "$work/bsddate" "$home/bin/xymongen"

cp "$work/gnudate" "$work/gnu/date"
cp "$work/bsddate" "$work/bsd/date"
cp "$work/bsddate" "$work/both/date"
cp "$work/gnudate" "$work/both/gdate"

export GNUDATE_LOG=$work/gnudate.log XYMONGEN_LOG=$work/xymongen.log
export XYMONHOME=$home XYMONWWWDIR=$home/www XYMONWEB=/xymon XYMONGENREPOPTS=""

# run_reports DIR [GNUDATE=value] -- run the daily report with DIR and the
# tools directory as the whole PATH; sets status, err, and clears both logs
# first.
run_reports() {
	local dir=$1; shift
	: >"$GNUDATE_LOG"; : >"$XYMONGEN_LOG"
	set +e
	# Through $BASH, not the script's own #!/bin/bash: FreeBSD has no /bin/bash.
	env -u GNUDATE "$@" PATH="$dir:$work/tools" "$BASH" "$work/xymonreports.sh" daily \
		>"$work/out" 2>"$work/err"
	status=$?
	set -e
	err=$(cat "$work/err")
}

expected_opts="--reportopts=1762124400:1762210799:0:nongr $home/www/periodic/daily/2026/11/03"

# 1. gdate beside a BSD date: the script takes gdate, nothing configured.
run_reports "$work/both"
[ "$status" -eq 0 ] || fail "with gdate on PATH next to a BSD date, xymonreports.sh exited $status: $err"
grep "/both/gdate --version" "$GNUDATE_LOG" >/dev/null \
	|| fail "xymonreports.sh did not ask gdate --version before using it: $(cat "$GNUDATE_LOG")"
[ "$(cat "$XYMONGEN_LOG")" = "$expected_opts" ] \
	|| fail "with gdate on PATH, xymongen was run as '$(cat "$XYMONGEN_LOG")', expected '$expected_opts'"

# 2. Linux: date itself is GNU date, and is used as before.
run_reports "$work/gnu"
[ "$status" -eq 0 ] || fail "with a GNU date, xymonreports.sh exited $status: $err"
[ "$(cat "$XYMONGEN_LOG")" = "$expected_opts" ] \
	|| fail "with a GNU date, xymongen was run as '$(cat "$XYMONGEN_LOG")', expected '$expected_opts'"

# 3. GNUDATE set, as xymonserver.cfg would: used verbatim, PATH not searched.
run_reports "$work/bsd" GNUDATE="$work/gnudate"
[ "$status" -eq 0 ] || fail "with GNUDATE set, xymonreports.sh exited $status: $err"
grep "^$work/gnudate " "$GNUDATE_LOG" >/dev/null \
	|| fail "GNUDATE=$work/gnudate was set but not used: $(cat "$GNUDATE_LOG")"
[ "$(cat "$XYMONGEN_LOG")" = "$expected_opts" ] \
	|| fail "with GNUDATE set, xymongen was run as '$(cat "$XYMONGEN_LOG")', expected '$expected_opts'"

# 4. GNUDATE names a BSD date, a wrong line in xymonserver.cfg: stop, name
# GNUDATE, run nothing, instead of the old run with date's errors.
run_reports "$work/bsd" GNUDATE="$work/bsddate"
[ "$status" -eq 1 ] || fail "with GNUDATE naming a BSD date, xymonreports.sh exited $status, expected 1: $err"
case $err in
	*"GNUDATE=$work/bsddate is not GNU date"*) ;;
	*) fail "with GNUDATE naming a BSD date, stderr does not say which GNUDATE is wrong: '$err'" ;;
esac
[ ! -s "$XYMONGEN_LOG" ] || fail "with GNUDATE naming a BSD date, xymongen still ran: $(cat "$XYMONGEN_LOG")"

# 5. Only a BSD date and no GNUDATE: stop, say so, run nothing.
run_reports "$work/bsd"
[ "$status" -eq 1 ] || fail "with only a BSD date, xymonreports.sh exited $status, expected 1: $err"
case $err in
	*GNUDATE*xymonserver.cfg*) ;;
	*) fail "with only a BSD date, stderr does not say to set GNUDATE in xymonserver.cfg: '$err'" ;;
esac
[ ! -s "$XYMONGEN_LOG" ] || fail "with only a BSD date, xymongen still ran: $(cat "$XYMONGEN_LOG")"

pass "xymonreports.sh uses a GNUDATE from the environment that is GNU date, else the first GNU date among gdate and date on PATH, and stops with a message naming the problem otherwise"
