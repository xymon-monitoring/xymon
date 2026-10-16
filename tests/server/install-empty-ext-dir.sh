#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/install-empty-ext-dir.sh
#
# xymond's install-cfg completes when the ext/ directory is empty.
#
# install-cfg gave ext/ to XYMONUSER with "chown ... $(INSTALLEXTDIR)/*". The
# only file installed there is xymonnet's xymonnet-again.sh, so on a server
# built without xymonnet ext/ is empty: the glob matched nothing, reached chown
# as the literal path ".../ext/*", and the install failed with "cannot access".
#
# Installed for real into a scratch tree, every install path pointed inside it
# and owned by the user running the test. A staged install (INSTALLROOT) does
# not do here: its symlinks point at the final paths, outside the stage, and
# chown follows them. The full install puts xymonnet-again.sh in ext/; the
# test then empties ext/ and runs install-cfg again, which is the state a
# server without xymonnet installs in.
#
# The full install is a packager's one (PKGBUILD=1): without it, install-bin
# chowns the programs in the build tree, which fails for a user who does not
# own them -- after a "make install" as root, say. Only install-cfg, the step
# under test, runs its chowns, and they touch nothing outside the scratch tree.

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
dest=$work/inst
vars=(
	INSTALLROOT= PKGBUILD=
	XYMONUSER="$(id -un)" HTTPDGID="$(id -gn)"
	XYMONTOPDIR="$dest/top" XYMONHOME="$dest/top/server" XYMONVAR="$dest/var"
	CGIDIR="$dest/top/cgi-bin" SECURECGIDIR="$dest/top/cgi-secure"
	XYMONLOGDIR="$dest/log" XYMONRUNDIR="$dest/run" MANROOT="$dest/man"
	INSTALLBINDIR="$dest/top/server/bin" INSTALLETCDIR="$dest/etc"
	INSTALLEXTDIR="$dest/top/server/ext" INSTALLTMPDIR="$dest/var/tmp"
	INSTALLWEBDIR="$dest/etc/web" INSTALLWWWDIR="$dest/var/www"
)

"$XYMON_MAKE" -C "$ROOT" install "${vars[@]}" PKGBUILD=1 >"$work/install.log" 2>&1 \
	|| fail "a full install into a scratch tree failed, before ext/ was emptied: $(tail -n 5 "$work/install.log")"
[ -d "$dest/top/server/ext" ] || fail "the install did not create ext/ at $dest/top/server/ext"

rm -f "$dest/top/server/ext/"*
"$XYMON_MAKE" -C "$ROOT/xymond" install-cfg "${vars[@]}" >"$work/install-cfg.log" 2>&1 \
	|| fail "xymond's install-cfg fails when ext/ is empty, as on a server built without xymonnet: $(grep -E -m 1 'cannot|Error' "$work/install-cfg.log" || tail -n 1 "$work/install-cfg.log")"

pass "xymond's install-cfg completes with an empty ext/ directory"
