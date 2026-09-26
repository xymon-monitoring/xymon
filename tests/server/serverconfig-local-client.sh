#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/serverconfig-local-client.sh
#
# On the Xymon server, "where do I send" and "who am I" are different
# questions, and the shipped config must not answer them with one value.
#
#   XYMSRV       where the client on this host sends. Loopback, so it keeps
#                reporting when a physical interface goes away.
#   MACHINEADDR  how this host identifies itself in what it sends. Never
#                loopback: every machine has 127.0.0.0/8, so an address from
#                that range names no one.
#
# Both defaulted to $XYMONSERVERIP, and that shared default is the whole
# coupling -- which is why xymonserver.cfg.DIST used to carry the warning
# "use the real one, not 127.0.0.1" on XYMONSERVERIP. The warning was
# protecting MACHINEADDR, and it kept XYMSRV off loopback as a side effect.
#
# The client's own config is separate (client/xymonclient.cfg.DIST), so none
# of this reaches a remote client: there XYMSRV is the server's address and
# must stay that way. Checked here too, since the whole point is that the two
# files answer differently.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRVCFG="$ROOT/xymond/etcfiles/xymonserver.cfg.DIST"
CLICFG="$ROOT/client/xymonclient.cfg.DIST"
[ -f "$SRVCFG" ] || skip "xymonserver.cfg.DIST not present in this checkout"
[ -f "$CLICFG" ] || skip "xymonclient.cfg.DIST not present in this checkout"

# setting FILE NAME -- the value assigned to NAME, unquoted, comments stripped.
setting() {
	sed -n "s/^$2=\"\{0,1\}\([^\"#]*\)\"\{0,1\}.*/\1/p" "$1" | head -1 | sed 's/[[:space:]]*$//'
}

srv_xymsrv=$(setting "$SRVCFG" XYMSRV)
srv_machineaddr=$(setting "$SRVCFG" MACHINEADDR)
cli_xymsrv=$(setting "$CLICFG" XYMSRV)

# --- the server sends over loopback -------------------------------------------

case "$srv_xymsrv" in
	127.*) ;;
	*) fail "the server's XYMSRV is '$srv_xymsrv', so the client on the server reaches xymond over a physical interface and stops reporting when it goes down" ;;
esac

# --- but does not identify itself as loopback ---------------------------------

case "$srv_machineaddr" in
	127.*) fail "the server's MACHINEADDR is '$srv_machineaddr'; no address in 127.0.0.0/8 identifies a machine" ;;
esac
[ "$srv_xymsrv" != "$srv_machineaddr" ] || \
	fail "XYMSRV and MACHINEADDR are both '$srv_xymsrv'; they answer different questions and must not share a value"

# --- and a remote client still sends to the server -----------------------------

case "$cli_xymsrv" in
	127.*) fail "the client config's XYMSRV is '$cli_xymsrv'; a remote client must send to the server, not to itself" ;;
esac

# --- the built-in fallback is unchanged ---------------------------------------

# lib/environ.c is shared by both, so its default must stay the server address:
# a client whose config omits XYMSRV has to reach the server, not loopback.
assert_contains '{ "XYMSRV", "$XYMONSERVERIP" }' "$(cat "$ROOT/lib/environ.c")" \
	"the compiled-in XYMSRV default is shared with clients and must remain the server address"

pass "the server sends over loopback and still identifies itself by its real address"
