#!/usr/bin/env bash
# An unreadable protocols.cfg is a config fault, not an outage.
#
# Nothing can be checked without the file, and the honest report is yellow
# naming the file. Red says the services are down: it pages someone, and it
# sends them to look at the servers instead of at the file that is missing.
#
# This used to depend on a compiled-in list of 28 names. A service on that
# list went yellow and a service not on it went red -- same cause, two
# colours, decided by which names someone hardcoded years ago. Both names
# below are checked for that reason: "pop3" was on the list, "amqp" was not.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

# No protocols.cfg at all.
printf '127.0.0.1\thh\t# pop3 amqp\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" XYMONNETSVCS="pop3 amqp" "$XYMONNET" \
	--no-update --noping --dns=ip --timeout=6 >"$work/out" 2>&1 || :

for svc in pop3 amqp; do
	colour=$(grep -aoE "hh\.$svc (green|yellow|red|clear)" "$work/out" | awk '{print $2}' | head -1)
	[ -n "$colour" ] || fail "no verdict at all for $svc:
$(head -10 "$work/out")"
	[ "$colour" = "yellow" ] || fail \
		"$svc reported $colour with no protocols.cfg to read. Nothing was
checked, so red claims an outage that was never observed and points the
operator at the service rather than at the missing file:
$(grep -a "hh\.$svc" "$work/out" | head -2)"
done

grep -aq 'protocols.cfg' "$work/out" || fail \
	"the reason never names protocols.cfg, so the report says a check failed
without saying that no check was possible:
$(grep -a 'is not OK' "$work/out" | head -3)"

pass "an unreadable protocols.cfg reports yellow and names the file, for any service"
