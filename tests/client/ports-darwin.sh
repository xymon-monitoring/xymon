#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/ports-darwin.sh
#
# The [ports] section of xymonclient-darwin.sh. On macOS 27 the socket-table
# sysctls behind `netstat -an` answer only processes with an Apple-signed
# ancestry; under xymonlaunch (ad-hoc signed in any Homebrew or source build)
# netstat prints the UNIX and Multipath tables and not one Internet socket, so
# the section arrived empty on every run. The script then takes one nettop
# sample, which the framework behind it still serves in full, and rewrites it
# into netstat's column layout, because that layout is what the server's ports
# test parses (unix_ports_report: columns 3, 4, 5).
#
# netstat and nettop are stubbed on PATH; the fixtures are what a real
# Apple-silicon Mac printed (2026-10). The section is cut out of the script and
# run on its own, as the df tests do.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

SCRIPT="${XYMONCLIENT_DARWIN:-$(find_root)/client/xymonclient-darwin.sh}"
[ -f "$SCRIPT" ] || skip "$SCRIPT missing"
grep -q 'nettop -L 1' "$SCRIPT" || fail "nettop fallback missing from $SCRIPT (regressed)"

TMP=$(mktempdir)
STUB="$TMP/bin"
mkdir -p "$STUB"
SNIPPET="$TMP/ports-section.sh"
sed -n '/^echo "\[ports\]"/,/^echo "\[ps\]"/p' "$SCRIPT" | sed '$d' > "$SNIPPET"
grep -q 'netstat -an' "$SNIPPET" || fail "could not cut the [ports] section out of $SCRIPT"

NETTOP_CALLS="$TMP/nettop.calls"
: > "$NETTOP_CALLS"

# netstat as macOS 27 prints it to an ad-hoc signed ancestry: no inet table at
# all. NETSTAT_INET=1 adds the table a shell gets, for the no-fallback case.
cat > "$STUB/netstat" <<'NS'
#!/usr/bin/env bash
if [ "${NETSTAT_INET:-0}" = 1 ]; then
cat <<'OUT'
Active Internet connections (including servers)
Proto Recv-Q Send-Q  Local Address          Foreign Address        (state)    
tcp4       0      0  127.0.0.1.1984         *.*                    LISTEN     
udp4       0      0  *.5353                 *.*                               
OUT
fi
cat <<'OUT'
Active Multipath Internet connections
Proto/ID  Flags      Local Address          Foreign Address        (state)    
Active LOCAL (UNIX) domain sockets
Address          Type   Recv-Q Send-Q            Inode             Conn             Refs          Nextref Addr
73fb4ee211694060 stream      0      0                0 e34697b0abf475c1                0                0
OUT
NS
chmod +x "$STUB/netstat"

# nettop -L 1 -n -J state: a header, one row per process, one row per socket
# under it. IPv4 ends are addr:port, IPv6 ends addr.port, states CamelCase.
cat > "$STUB/nettop" <<'NT'
#!/usr/bin/env bash
echo "$*" >> "${NETTOP_CALLS:?}"
[ "${NETTOP_EMPTY:-0}" = 1 ] && exit 1
cat <<'OUT'
,state,
launchd.1,,
tcp4 *:22<->*:*,Listen,
tcp6 *.22<->*.*,Listen,
tcp4 127.0.0.1:8021<->*:*,Listen,
tcp6 ::1.8021<->*.*,Listen,
RustDesk.1915,,
tcp6 172.26.72.104.21118<->172.26.72.31.59960,Established,
Safari.1650,,
tcp4 172.26.72.104:59476<->17.248.209.74:443,Established,
tcp4 172.26.72.104:59480<->17.248.209.74:443,CloseWait,
tcp4 172.26.72.104:59481<->17.248.209.74:443,FinWait1,
tcp4 172.26.72.104:59482<->17.248.209.74:443,SynReceived,
mDNSResponder.665,,
udp4 *:5353<->*:*,,
udp6 *.5353<->*.*,,
OUT
NT
chmod +x "$STUB/nettop"

run_section() { env PATH="$STUB:$PATH" NETTOP_CALLS="$NETTOP_CALLS" "$@" sh "$SNIPPET" 2>"$TMP/stderr"; }
row() { printf '%s\n' "$1" | grep -- "$2"; }

# --- netstat answers: its lines pass through, nettop is never asked -----------
out=$(run_section NETSTAT_INET=1)
assert_match '^tcp4 +0 +0 +127\.0\.0\.1\.1984 +\*\.\* +LISTEN' "$(row "$out" 1984)" "netstat's own tcp line is passed through unchanged"
assert_match '^udp4 +0 +0 +\*\.5353 +\*\.\*' "$(row "$out" 5353)" "netstat's own udp line is passed through unchanged"
assert_not_contains "Active" "$out" "netstat's table headers are not emitted"
assert_equal '0' "$(($(wc -l < "$NETTOP_CALLS")))" "nettop is not run while netstat lists sockets"

# --- netstat lists no Internet socket: one nettop sample in netstat's layout --
: > "$NETTOP_CALLS"
out=$(run_section)
assert_equal '1' "$(($(wc -l < "$NETTOP_CALLS")))" "nettop is run once when netstat has no Internet sockets"
assert_contains "-L 1" "$(cat "$NETTOP_CALLS")" "nettop is asked for a single sample"
assert_contains "-n" "$(cat "$NETTOP_CALLS")" "with numeric addresses"
assert_equal 'tcp4 0 0 *.22 *.* LISTEN' "$(row "$out" '^tcp4 .*22 ')" \
	"a root-owned IPv4 listener is present, *:22 becomes *.22, the remote end *.*"
assert_equal 'tcp6 0 0 *.22 *.* LISTEN' "$(row "$out" '^tcp6 .*22 ')" \
	"its IPv6 twin keeps nettop's dotted spelling"
assert_equal 'tcp4 0 0 127.0.0.1.8021 *.* LISTEN' "$(row "$out" '127.0.0.1')" \
	"an IPv4 address: only the last colon becomes the port separator"
assert_equal 'tcp6 0 0 ::1.8021 *.* LISTEN' "$(row "$out" '::1')" \
	"an IPv6 address keeps its colons"
assert_equal 'tcp6 0 0 172.26.72.104.21118 172.26.72.31.59960 ESTABLISHED' "$(row "$out" 59960)" \
	"an established connection: the <-> pair becomes the local and remote columns"
assert_equal 'tcp4 0 0 172.26.72.104.59476 17.248.209.74.443 ESTABLISHED' "$(row "$out" 59476)" \
	"and both IPv4 ends get the dot"
assert_equal 'CLOSE_WAIT'  "$(row "$out" 59480 | awk '{print $6}')" "CamelCase state CloseWait becomes netstat's CLOSE_WAIT"
assert_equal 'FIN_WAIT_1'  "$(row "$out" 59481 | awk '{print $6}')" "a trailing digit is set off too: FinWait1 becomes FIN_WAIT_1"
assert_equal 'SYN_RCVD'    "$(row "$out" 59482 | awk '{print $6}')" "SynReceived becomes netstat's abbreviation SYN_RCVD"
assert_equal 'udp4 0 0 *.5353 *.*' "$(row "$out" '^udp4')" "a UDP socket has no state column and no trailing blank"
assert_equal 'udp6 0 0 *.5353 *.*' "$(row "$out" '^udp6')" "for IPv6 as well"
assert_not_contains "launchd" "$out" "nettop's process rows are dropped"
assert_not_contains "state" "$out" "and its header"
assert_equal '11' "$(printf '%s\n' "$out" | grep -c -e '^tcp' -e '^udp')" "every nettop socket row becomes exactly one netstat-style row"

# --- neither answers: the header and nothing else, quietly ---------------------
out=$(run_section NETTOP_EMPTY=1)
assert_equal '[ports]' "$out" "with nothing from netstat or nettop the section is its header and nothing else"
assert_equal '' "$(cat "$TMP/stderr")" "and nothing is written to stderr"

pass "xymonclient-darwin.sh [ports]: netstat's list when it has one, else one nettop sample rewritten into netstat's columns"
