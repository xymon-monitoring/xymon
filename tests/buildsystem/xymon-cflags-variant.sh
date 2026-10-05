#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/xymon-cflags-variant.sh
#
# xymon_cflags() hands a harness the tree's CLIENTONLY and LOCALCLIENT
# defines, and nothing more of its CFLAGS.
#
# build/Makefile.rules adds -DCLIENTONLY=1 to a client tree's CFLAGS and
# -DLOCALCLIENT=1 to a localclient one, and the headers branch on them:
# lib/loadalerts.h includes <pcre2.h> only for a server or localclient build.
# Without the defines a harness compiled on a client tree asked for a header
# the client build never needs, and failed wherever PCRE was not installed --
# four tests/libxymon/ harnesses on the client lanes (#375).
#
# The helper asks make, so the fixtures are trees with a minimal Makefile
# whose CFLAGS stand for what Makefile.rules produces for each variant.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_gnu_make

work=$(mktempdir)

# fixture NAME CFLAGS -- a tree whose toplevel Makefile carries CFLAGS.
fixture() {
	mkdir -p "$work/$1"
	printf 'CFLAGS = %s\n' "$2" > "$work/$1/Makefile"
}

fixture server      "-O2 -DXYMON_SERVER_ONLY_SENTINEL"
fixture client      "-O2 -DCLIENTONLY=1 -DXYMON_SERVER_ONLY_SENTINEL"
fixture localclient "-O2 -DCLIENTONLY=1 -DLOCALCLIENT=1"
mkdir -p "$work/unconfigured"

server=$(xymon_cflags "$work/server")
client=$(xymon_cflags "$work/client")
localclient=$(xymon_cflags "$work/localclient")
unconfigured=$(xymon_cflags "$work/unconfigured")

assert_contains "-DCLIENTONLY=1" "$client" \
	"a client tree's harness flags lack its -DCLIENTONLY=1, so lib/loadalerts.h asks for <pcre2.h> the client build never needs"
assert_contains "-DCLIENTONLY=1" "$localclient" \
	"a localclient tree's harness flags lack its -DCLIENTONLY=1"
assert_contains "-DLOCALCLIENT=1" "$localclient" \
	"a localclient tree's harness flags lack its -DLOCALCLIENT=1"

case $client in *-DLOCALCLIENT*)
	fail "a client tree's harness flags carry -DLOCALCLIENT, which only a localclient tree defines: $client" ;;
esac
for flags in "$server" "$unconfigured"; do
	case $flags in *-DCLIENTONLY*|*-DLOCALCLIENT*)
		fail "a server or unconfigured tree's harness flags carry a client define: $flags" ;;
	esac
done

# Only those two: the rest of CFLAGS stays out, so a server tree's compile
# lines are what they were.
case "$server $client" in *XYMON_SERVER_ONLY_SENTINEL*|*-O2*)
	fail "xymon_cflags passed on more of the tree's CFLAGS than the two client defines: $server | $client" ;;
esac

pass "xymon_cflags carries CLIENTONLY on client trees, CLIENTONLY and LOCALCLIENT on localclient trees, and no other CFLAGS"
