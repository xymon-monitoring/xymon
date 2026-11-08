#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-client-transport.sh
#
# The client data page says how the client message came. With a real
# xymond behind svcstatus.cgi, checked:
#
#   - after a plaintext client message from realhost, the page starts with
#     "[transport]" and "plaintext from 127.0.0.1", then the message itself,
#     unchanged;
#   - SECTION= asks for one section, so the page is that section alone,
#     with no "[transport]" added;
#   - the line is added to the page only: xymond's own "clientlog" answer,
#     which other tools read, does not have it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

svcstatus_setup --no-daemon
svcstatus_build || { cat "$work/cc.log" >&2; fail "svcstatus does not build"; }

P=$(free_port)
echo "XYMONDPORT=\"$P\"" >>"$work/etc/xymonserver.cfg"

mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymond.cfg"

MLPID=
"$XYMOND" --no-daemon --listen="127.0.0.1:$P" \
	--hosts="$work/etc/hosts.cfg" --env="$work/xymond.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
	> "$work/xymond.log" 2>&1 &
MLPID=$!
stop() { [ -n "$MLPID" ] || return 0; kill "$MLPID" 2>/dev/null || true; wait "$MLPID" 2>/dev/null || true; MLPID=; }
register_cleanup 'stop'
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && break
	kill -0 "$MLPID" 2>/dev/null || fail "xymond did not start: $(cat "$work/xymond.log")"
	sleep 0.1
done
grep -q 'Setup complete' "$work/xymond.log" || fail "xymond did not start: $(cat "$work/xymond.log")"

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" "$1"; }

x "client realhost.linux linux
[date]
Sun Nov  8 10:00:00 UTC 2026
[uptime]
 10:00  up 1 day" >/dev/null
sleep 0.5

render_live "CLIENT=realhost"
first=$(printf '%s\n' "$OUT" | sed -n '/^Content-type/,$p' | sed '1,2d' | sed -n 1,2p | tr '\n' '/')
[ "$first" = "[transport]/plaintext from 127.0.0.1/" ] \
	|| fail "the client page starts '$first', not '[transport]/plaintext from 127.0.0.1/'"
assert_contains "[uptime]" "$OUT" "the client message itself is missing below the transport line"

render_live "CLIENT=realhost&SECTION=uptime"
assert_not_contains "[transport]" "$OUT" "one section was asked for, and the page added the transport line to it"
assert_contains "up 1 day" "$OUT" "SECTION=uptime did not return the uptime section"

got=$(x "clientlog realhost")
case $got in *"[transport]"*) fail "xymond's own clientlog answer has the transport line: it belongs on the page only" ;; esac

pass "the client data page says how the client message came; one section, and xymond's own answer, are left as they were"
