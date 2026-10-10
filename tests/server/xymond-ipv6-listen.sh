#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-ipv6-listen.sh
#
# xymond listens on an IPv6 address when --listen names one, and checks an
# IPv6 sender against the sender lists like any other -- including the rule
# that a host may report on itself, from the IPv6 address hosts.cfg gives it.
#
# --listen used to take IPv4 addresses only, and a connection's address, the
# sender lists and the check of each message against them were IPv4 too. Now
# "[::1]:PORT" is a listener of its own, next to an IPv4 one on the same port
# (an IPv6 socket takes IPv6 only), and --status-senders and the other lists
# accept IPv6 addresses and networks.
#
# The check that matters most: oksender() lets the IPv4 address 0.0.0.0
# through unchecked, because that is how xymond marks a message from its own
# backfeed channel. An IPv6 sender read as an empty IPv4 address would pass
# every list. So a status from ::1 must be refused by a list that names only
# 127.0.0.1, and accepted by one that names ::1.
#
# The xymon client here is IPv4-only, so the test sends with a small client
# of its own. Where ::1 cannot be bound, only the IPv4 side is checked, and
# the run says so.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_cc
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost # conn\n::1 v6host # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

# send ADDRESS PORT MESSAGE -- send it as the xymon client does (write, then
# half-close) and print the reply; ADDRESS may be IPv4 or IPv6.
cat >"$work/send.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
int main(int argc, char **argv)
{
	struct addrinfo hints, *ai; char buf[4096]; ssize_t n; int s;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_NUMERICHOST;
	if (argc != 4 || getaddrinfo(argv[1], argv[2], &hints, &ai) != 0) return 2;
	if ((s = socket(ai->ai_family, SOCK_STREAM, 0)) < 0 || connect(s, ai->ai_addr, ai->ai_addrlen) != 0) return 1;
	if (write(s, argv[3], strlen(argv[3])) < 0) return 1;
	shutdown(s, SHUT_WR);
	while ((n = read(s, buf, sizeof(buf))) > 0) fwrite(buf, 1, n, stdout);
	close(s);
	return 0;
}
EOF
"$CC" -o "$work/send" "$work/send.c" 2>"$work/cc.log" || fail "the test client does not compile: $(cat "$work/cc.log")"

launch() {
	"$XYMOND" --no-daemon "$@" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		> "$work/xymond.log" 2>&1 &
	MLPID=$!
	i=0
	while [ "$i" -lt 100 ]; do
		grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && return 0
		kill -0 "$MLPID" 2>/dev/null || return 1
		sleep 0.1
		i=$((i+1))
	done
	return 1
}
MLPID=
stop() { [ -n "$MLPID" ] || return 0; kill "$MLPID" 2>/dev/null || true; wait "$MLPID" 2>/dev/null || true; MLPID=; }
register_cleanup 'stop'

# status ADDRESS -- send a status for testhost.cpu from ADDRESS, then ask
# (over IPv4, which no list here restricts for queries) whether it was kept
status() {
	"$work/send" "$1" "$P" "status testhost.cpu green sent from $1" >/dev/null || true
	sleep 0.5
	"$work/send" 127.0.0.1 "$P" "xymondlog testhost.cpu" || true
}

P=$(free_port)

# IPv4 and IPv6 on the same port: the IPv6 socket must not take IPv4 too.
if ! launch --listen="127.0.0.1:$P,[::1]:$P" --status-senders=127.0.0.1; then
	if grep -q 'Cannot bind to listen socket ::1' "$work/xymond.log"; then
		why=$(grep 'Cannot bind to listen socket ::1' "$work/xymond.log" | sed 's/^.*Cannot bind/Cannot bind/')
		launch --listen="127.0.0.1:$P" || { cat "$work/xymond.log" >&2; fail "xymond did not start on IPv4"; }
		case "$("$work/send" 127.0.0.1 "$P" ping)" in *xymond*) ;; *) fail "xymond does not answer on 127.0.0.1:$P" ;; esac
		stop
		pass_partial "xymond still listens on IPv4" \
			"IPv6: this host has no IPv6 loopback ($why)"
	fi
	cat "$work/xymond.log" >&2
	fail "xymond did not start with an IPv4 and an IPv6 listener on the same port"
fi
grep -q "Listening on ::1:$P" "$work/xymond.log" || fail "xymond did not report its IPv6 listener: $(cat "$work/xymond.log")"
case "$("$work/send" ::1 "$P" ping)" in *xymond*) ;; *) fail "xymond does not answer a ping on [::1]:$P" ;; esac
case "$("$work/send" 127.0.0.1 "$P" ping)" in *xymond*) ;; *) fail "xymond does not answer a ping on 127.0.0.1:$P next to its IPv6 listener" ;; esac

# A list naming only 127.0.0.1 refuses ::1 -- not waved through as the backfeed.
got=$(status ::1)
assert_not_contains "sent from ::1" "$got" "a status from ::1 was kept although --status-senders names only 127.0.0.1"
grep -q 'Refused message from ::1' "$work/xymond.log" \
	|| fail "the status from ::1 was not refused with its address: $(cat "$work/xymond.log")"
got=$(status 127.0.0.1)
assert_contains "sent from 127.0.0.1" "$got" "a status from an allowed IPv4 sender was not kept"

# ... but a host may report on itself, from the IPv6 address hosts.cfg gives it.
# xymond applies that rule only to a host it already has a record for (on main
# too: the sender is checked before a first status creates one), so the record
# is made first, by a sender the list allows.
"$work/send" 127.0.0.1 "$P" "status v6host.conn green from an allowed sender" >/dev/null || true
sleep 0.3
"$work/send" ::1 "$P" "status v6host.cpu green v6host reporting on itself" >/dev/null || true
sleep 0.5
got=$("$work/send" 127.0.0.1 "$P" "xymondlog v6host.cpu" || true)
assert_contains "v6host reporting on itself" "$got" \
	"v6host, listed as ::1, was refused a status about itself from ::1 -- a host may always report on itself"
stop

# A list naming ::1, among an IPv6 network, accepts it.
launch --listen="127.0.0.1:$P,[::1]:$P" --status-senders="127.0.0.1,2001:db8::/32,::1" \
	|| { cat "$work/xymond.log" >&2; fail "xymond did not start with IPv6 entries in --status-senders"; }
got=$(status ::1)
assert_contains "sent from ::1" "$got" "a status from ::1 was refused although --status-senders names ::1"
stop

pass "xymond listens on [::1] next to 127.0.0.1 on the same port, checks an IPv6 sender against IPv6 entries of the sender lists, never letting it through as the backfeed, and lets a host report on itself from its IPv6 address"
