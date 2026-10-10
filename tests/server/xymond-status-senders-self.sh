#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-status-senders-self.sh
#
# With --status-senders, a host may send its own first status.
#
# A sender that is not in --status-senders may still report on the host it
# is: oksender() accepts a message whose host has the sender's address in
# hosts.cfg. xymond took that address from the host's record, and checked
# the sender before a first status had created one, so a host nothing else
# had reported on yet was refused even from its own address -- for ever, if
# nothing else ever reported on it, as with a client-only host. The address
# now comes from hosts.cfg, record or not.
#
# selfhost is listed at 127.0.0.1, where the client sends from; the list
# names only 192.0.2.1. Its first status must be kept, and one for
# otherhost, listed elsewhere, must still be refused.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 selfhost # conn\n127.0.0.1 combohost # conn\n192.0.2.5 otherhost # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

P=$(free_port)
"$XYMOND" --no-daemon --listen="127.0.0.1:$P" --status-senders=192.0.2.1 \
	--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
	> "$work/xymond.log" 2>&1 &
pid=$!
register_cleanup "kill $pid 2>/dev/null || true"
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && break
	kill -0 "$pid" 2>/dev/null || fail "xymond did not start: $(cat "$work/xymond.log")"
	sleep 0.1
done

xymon() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" "$1" 2>&1 || true; }

xymon "status selfhost.cpu green selfhost reporting on itself" >/dev/null
xymon "status otherhost.cpu green not otherhost" >/dev/null
# Clients send a combo message, which xymond checks status by status.
xymon "combo
status combohost.cpu green combohost reporting on itself
" >/dev/null
sleep 0.5

got=$(xymon "xymondlog selfhost.cpu")
assert_contains "selfhost reporting on itself" "$got" \
	"selfhost's first status from its own address was refused: $(grep 'Refused' "$work/xymond.log" || true)"
grep -q 'Refused message from 127.0.0.1: status otherhost.cpu' "$work/xymond.log" \
	|| fail "a status for otherhost, listed at 192.0.2.5, was not refused from 127.0.0.1: $(cat "$work/xymond.log")"
got=$(xymon "xymondlog combohost.cpu")
assert_contains "combohost reporting on itself" "$got" \
	"combohost's first status, in a combo message from its own address, was refused: $(grep 'Refused' "$work/xymond.log" || true)"
assert_not_contains "Refused message from 127.0.0.1: status selfhost" "$(cat "$work/xymond.log")" \
	"selfhost's status was refused"

pass "with --status-senders, a host's first status from its own hosts.cfg address is kept, alone or in a combo message, and another host's from that address refused"
