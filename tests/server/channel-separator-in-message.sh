#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/channel-separator-in-message.sh
#
# xymond hands each message to its workers as a record: its own first line,
# the message, then a line "@@". A line of the message that starts with "@@"
# is now passed on starting with "@ " instead, so it stays inside the record
# it belongs to and only xymond's own framing lines start with "@@".
#
# A worker on the status channel writes what it receives to a file; the
# status sent holds a line "@@" and a line beginning "@@renametest". A worker
# on the client channel does the same for client data, which reaches the
# channel by another path through xymond (a record built before
# posttochannel()). A status with Windows line endings checks its lines are
# rewritten too; "\r\n@@\r\n" is not a record end, so it was one record
# either way.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMON common/xymon
XYMONCLIENT=$XYMON
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work"/{home/etc,home/tmp,var,logs}
printf '127.0.0.1 testhost # noconn\n' >"$work/home/etc/hosts.cfg"

xymond_launch() {
	local port=$1; shift
	cat >"$work/xymonserver.cfg" <<EOF
XYMONHOME="$work/home"
XYMONVAR="$work/var"
XYMONSERVERLOGS="$work/logs"
XYMONTMP="$work/home/tmp"
HOSTSCFG="!$work/home/etc/hosts.cfg"
XYMONSERVERHOSTNAME="testhost"
XYMONSERVERIP="127.0.0.1"
MACHINEDOTS="testhost"
XYMONDPORT="$port"
EOF
	"$XYMOND" --env="$work/xymonserver.cfg" --hosts="$work/home/etc/hosts.cfg" \
		--listen="127.0.0.1:$port" --pidfile="$work/xymond.pid" --no-daemon "$@" \
		>"$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

stop_daemons() {
	"$XYMON" "127.0.0.1:${PORT:-}" shutdown >/dev/null 2>&1 || true
	for pidfile in "$work/channel.pid" "$work/client-channel.pid" "$work/xymond.pid"; do
		if [ -f "$pidfile" ]; then
			stop_process "$(cat "$pidfile")"
		fi
	done
}
register_cleanup stop_daemons

start_xymond

# The worker: everything the status channel delivers, appended to a file
cat >"$work/capture" <<EOF
#!/bin/sh
exec cat >>"$work/received"
EOF
chmod +x "$work/capture"
"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=status \
	--pidfile="$work/channel.pid" --daemon --log="$work/logs/channel.log" \
	"$work/capture"

# Wait until the worker is attached: statuses sent before are not delivered
for _ in $(seq 1 100); do
	"$XYMON" "127.0.0.1:$PORT" "status testhost.ready green attached" >/dev/null 2>&1 || true
	grep -q 'attached' "$work/received" 2>/dev/null && break
	sleep 0.1
done
grep -q 'attached' "$work/received" 2>/dev/null || fail "the status channel worker received nothing: $(cat "$work/logs/channel.log" 2>/dev/null)"

"$XYMON" "127.0.0.1:$PORT" "$(printf 'status testhost.forgery green before\n@@\n@@renametest|0|127.0.0.1|testhost|forgery|other\nafter')"
for _ in $(seq 1 100); do
	grep -q '^after' "$work/received" 2>/dev/null && break
	sleep 0.1
done
received=$(cat "$work/received")
assert_contains "after" "$received" "the status reached the worker"

# Between "before" and "after", nothing ends the record or starts a new one
inside=$(awk '/green before/{f=1} f{print} /^after/{exit}' "$work/received")
assert_not_contains $'\n@@\n' "$inside" "a line \"@@\" in the status does not end its record"
if grep -q '^@@renametest' <<<"$received"; then
	fail "a line of the status reached the worker as an @@renametest record"
fi
assert_contains $'\n@ renametest|' "$inside" "the line reaches the worker inside its record, starting with \"@ \""

# Windows line endings. A worker ends a record only at "\n@@\n", which
# "\r\n@@\r\n" is not, so this status was one record before the change too.
# Its lines are rewritten all the same, since they hold "\n@@": checked here
# as the rewrite reaching them, not as an injection prevented.
"$XYMON" "127.0.0.1:$PORT" "$(printf 'status testhost.crlf green crlfbefore\r\n@@\r\n@@renametest|0|127.0.0.1|testhost|crlf|other\r\ncrlfafter')"
for _ in $(seq 1 100); do
	grep -q '^crlfafter' "$work/received" 2>/dev/null && break
	sleep 0.1
done
received=$(cat "$work/received")
assert_contains "crlfafter" "$received" "the status with Windows line endings reached the worker"
inside=$(awk '/green crlfbefore/{f=1} f{print} /^crlfafter/{exit}' "$work/received")
assert_not_contains $'\n@@\r' "$inside" "a line starting with \"@@\" before CR LF is rewritten too"
if grep -q '^@@renametest' <<<"$received"; then
	fail "a line of the status with Windows line endings reached the worker still starting with \"@@\""
fi
assert_contains $'\n@ renametest|0|127.0.0.1|testhost|crlf' "$inside" "with Windows line endings, the line starts with \"@ \""

# Client data, with its own worker on the client channel
printf '#!/bin/sh\nexec cat >>"%s"\n' "$work/received-client" >"$work/capture-client"
chmod +x "$work/capture-client"
"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=client \
	--pidfile="$work/client-channel.pid" --daemon --log="$work/logs/client-channel.log" \
	"$work/capture-client"
for _ in $(seq 1 100); do
	"$XYMON" "127.0.0.1:$PORT" "$(printf 'client testhost.linux\n[ready]\nclientattached')" >/dev/null 2>&1 || true
	grep -q 'clientattached' "$work/received-client" 2>/dev/null && break
	sleep 0.1
done
grep -q 'clientattached' "$work/received-client" 2>/dev/null || fail "the client channel worker received nothing: $(cat "$work/logs/client-channel.log" 2>/dev/null)"

"$XYMON" "127.0.0.1:$PORT" "$(printf 'client testhost.linux\n[test]\nclientbefore\n@@\n@@renametest|0|127.0.0.1|testhost|client|other\nclientafter')"
for _ in $(seq 1 100); do
	grep -q '^clientafter' "$work/received-client" 2>/dev/null && break
	sleep 0.1
done
received=$(cat "$work/received-client")
assert_contains "clientafter" "$received" "the client data reached the worker"
inside=$(awk '/^clientbefore/{f=1} f{print} /^clientafter/{exit}' "$work/received-client")
assert_not_contains $'\n@@\n' "$inside" "a line \"@@\" in client data does not end its record"
if grep -q '^@@renametest' <<<"$received"; then
	fail "a line of the client data reached the worker as an @@renametest record"
fi
assert_contains $'\n@ renametest|0|127.0.0.1|testhost|client' "$inside" "in client data, the line starts with \"@ \""

pass "a line starting with \"@@\" stays inside its record and does not start a new one, in a status and in client data, and is rewritten with Windows line endings too"
