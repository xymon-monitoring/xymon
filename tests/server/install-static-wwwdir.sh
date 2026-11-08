#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/install-static-wwwdir.sh
#
# INSTALLSTATICWWWDIR installs the web content Xymon ships -- gifs, help,
# menu -- apart from what xymongen and the CGIs write, and links it back.
#
# Three installs into scratch trees, every install path inside them and owned
# by the user running the test (a staged INSTALLROOT install does not do: its
# symlinks point outside the stage, and the install's chown follows them):
#
#   - unset: gifs, help and menu are real directories in the www tree, as
#     they always were, and the installed xymonserver.cfg carries the
#     XYMONSTATICWWWDIR the tree was built with;
#   - set: the content is in the static directory, the www tree holds
#     symlinks to it, and the directories xymongen writes stay real.
#     xymonserver.cfg is generated when the tree is built, so a value given
#     only at install time moves the files but not the setting; help still
#     resolves through the www/help link, which is what is checked here;
#   - a standalone "make install-docs" with it set still links www/help,
#     which the help links on the web pages are read through;
#   - "make -C xymond" and "make -C docs" alone, without the top-level
#     Makefile that sets the default, install into the www tree too, not at
#     the root of the install. This one is staged under INSTALLROOT, so a
#     regression lands in the stage rather than in /.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
# The install runs chown, which is in /usr/sbin or /sbin outside Linux, and
# a user's PATH may not hold either.
PATH=$PATH:/usr/sbin:/sbin
require_gnu_make
require_bin XYMOND xymond/xymond

work=$(mktempdir)

# install_vars DEST -- the install paths, all inside DEST, one per line.
install_vars() {
	local d=$1
	printf '%s\n' INSTALLROOT= PKGBUILD= \
		"XYMONUSER=$(id -un)" "HTTPDGID=$(id -gn)" \
		"XYMONTOPDIR=$d/top" "XYMONHOME=$d/top/server" "XYMONVAR=$d/var" \
		"CGIDIR=$d/top/cgi-bin" "SECURECGIDIR=$d/top/cgi-secure" \
		"XYMONLOGDIR=$d/log" "XYMONRUNDIR=$d/run" "MANROOT=$d/man" \
		"INSTALLBINDIR=$d/top/server/bin" "INSTALLETCDIR=$d/etc" \
		"INSTALLEXTDIR=$d/top/server/ext" "INSTALLTMPDIR=$d/var/tmp" \
		"INSTALLWEBDIR=$d/etc/web" "INSTALLWWWDIR=$d/var/www"
}

# run_make DEST LOG TARGET [VAR=VALUE...] -- make TARGET with DEST's paths.
run_make() {
	local d=$1 log=$2 target=$3 v
	shift 3
	local -a vars=()
	while IFS= read -r v; do vars+=("$v"); done <<<"$(install_vars "$d")"
	"$XYMON_MAKE" -C "$ROOT" "$target" "${vars[@]}" "$@" >"$log" 2>&1
}

# run_install DEST LOG [VAR=VALUE...] -- a full install into DEST. The install
# itself is a packager's one (PKGBUILD=1): without it, install-bin chowns the
# programs in the build tree, which fails for a user who does not own them --
# after a "make install" as root, say. The targets whose chowns this change
# touches -- install-dirs, install-docs and xymond's install-cfg -- then run
# without it, and chown nothing outside DEST.
run_install() {
	local d=$1 log=$2 v
	shift 2
	local -a vars=()
	while IFS= read -r v; do vars+=("$v"); done <<<"$(install_vars "$d")"
	"$XYMON_MAKE" -C "$ROOT" install "${vars[@]}" PKGBUILD=1 "$@" >"$log" 2>&1 \
		&& "$XYMON_MAKE" -C "$ROOT" install-docs "${vars[@]}" "$@" >>"$log" 2>&1 \
		&& "$XYMON_MAKE" -C "$ROOT/xymond" install-cfg "${vars[@]}" "$@" >>"$log" 2>&1
}

# staticdir_in CFG -- the XYMONSTATICWWWDIR value xymonserver.cfg was given.
staticdir_in() {
	sed -n 's/^XYMONSTATICWWWDIR="\(.*\)".*/\1/p' "$1"
}

# The static directory this tree was built with, as make computes it.
built_static=$("$XYMON_MAKE" -s --no-print-directory -C "$ROOT" -f Makefile -f - show-static <<<'show-static: ; @echo $(INSTALLSTATICWWWDIR)')
# A tree whose build files do not know the variable installs as if it were unset.
[ -n "$built_static" ] || built_static=$("$XYMON_MAKE" -s --no-print-directory -C "$ROOT" -f Makefile -f - show-www <<<'show-www: ; @echo $(INSTALLWWWDIR)')

# ---- unset: the content stays in the www tree ---------------------------------
d=$work/plain
run_install "$d" "$work/plain.log" \
	|| fail "the install without INSTALLSTATICWWWDIR failed: $(tail -n 3 "$work/plain.log")"
for sub in gifs help menu; do
	[ -d "$d/var/www/$sub" ] && [ ! -L "$d/var/www/$sub" ] \
		|| fail "without INSTALLSTATICWWWDIR, www/$sub must be a real directory, as before"
done
[ -n "$(ls -A "$d/var/www/gifs")" ] || fail "without INSTALLSTATICWWWDIR, www/gifs is empty"

# ---- set: the content moves, the www tree links to it --------------------------
d=$work/split
run_install "$d" "$work/split.log" "INSTALLSTATICWWWDIR=$d/static" \
	|| fail "the install with INSTALLSTATICWWWDIR failed: $(tail -n 3 "$work/split.log")"
for sub in gifs help menu; do
	[ -d "$d/static/$sub" ] && [ ! -L "$d/static/$sub" ] \
		|| fail "with INSTALLSTATICWWWDIR, $sub must be a real directory in the static tree"
	[ -L "$d/var/www/$sub" ] \
		|| fail "with INSTALLSTATICWWWDIR, www/$sub must be a symlink into the static tree"
	[ "$(cd "$d/var/www/$sub" && pwd -P)" = "$(cd "$d/static/$sub" && pwd -P)" ] \
		|| fail "with INSTALLSTATICWWWDIR, www/$sub must resolve to static/$sub"
done
[ -n "$(ls -A "$d/static/gifs")" ] || fail "with INSTALLSTATICWWWDIR, static/gifs is empty"
for sub in notes html wml; do
	[ -d "$d/var/www/$sub" ] && [ ! -L "$d/var/www/$sub" ] \
		|| fail "with INSTALLSTATICWWWDIR, www/$sub is written at run time and must stay a real directory"
done

# ---- the installed setting is the one the tree was built with ------------------
cfgval=$(staticdir_in "$work/plain/etc/xymonserver.cfg")
[ "$cfgval" = "$built_static" ] \
	|| fail "the installed xymonserver.cfg must carry XYMONSTATICWWWDIR=\"$built_static\", the value the tree was built with, got '$cfgval'"

# ---- a standalone install-docs still links help --------------------------------
d=$work/docs
run_make "$d" "$work/docs.log" install-docs "INSTALLSTATICWWWDIR=$d/static" \
	|| fail "a standalone install-docs with INSTALLSTATICWWWDIR failed: $(tail -n 3 "$work/docs.log")"
[ -L "$d/var/www/help" ] && [ -d "$d/var/www/help" ] \
	|| fail "a standalone install-docs left www/help without its link into the static tree"

# ---- the sub-makes alone fall back to the www tree -----------------------------
d=$work/sub
vars=()
while IFS= read -r v; do vars+=("$v"); done <<<"$(install_vars "$d")"
mkdir -p "$d/stage$d/etc/web" "$d/stage$d/top/server"
for sub in xymond:install-cfg docs:install; do
	"$XYMON_MAKE" -C "$ROOT/${sub%%:*}" "${sub#*:}" "${vars[@]}" INSTALLROOT="$d/stage" PKGBUILD=1 \
		>"$work/sub.log" 2>&1 \
		|| fail "make -C ${sub%%:*} ${sub#*:} failed: $(tail -n 3 "$work/sub.log")"
done
for sub in gifs help menu; do
	[ ! -e "$d/stage/$sub" ] \
		|| fail "make -C xymond or docs alone installed $sub at the root of the install, not in the www tree"
	[ -n "$(ls -A "$d/stage$d/var/www/$sub" 2>/dev/null)" ] \
		|| fail "make -C xymond or docs alone left www/$sub empty"
done

pass "INSTALLSTATICWWWDIR installs gifs, help and menu apart and links them back; unset, the www tree is as before"
