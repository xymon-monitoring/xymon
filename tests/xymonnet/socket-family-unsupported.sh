#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/socket-family-unsupported.sh
#
# An IPv6 host on a server without IPv6: socket(AF_INET6) fails there with
# EAFNOSUPPORT. do_tcp_tests() treated every socket() failure as running
# out of descriptors -- it lowered --concurrency and tried the same test
# again -- and this one never clears, so the run never ended and no host
# got a result. Checked:
#
#   - xymonnet finishes;
#   - the IPv6 host's test is reported as not OK, with the reason;
#   - an IPv4 host in the same run is still tested.
#
# A server without IPv6 is stood in for by a preloaded socket() that
# refuses AF_INET6 the way such a kernel does: LD_PRELOAD, or
# DYLD_INSERT_LIBRARIES and an interpose table on macOS; both are set and
# each system ignores the other. Where preloading does not take effect (a
# static build), a probe says so and the test skips.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir)
register_cleanup "rm -rf '$work'"

cat > "$work/noinet6.c" <<'EOF'
#define _GNU_SOURCE	/* RTLD_NEXT, on glibc */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifdef __APPLE__
static int noinet6(int domain, int type, int protocol)
{
	if (domain == AF_INET6) { errno = EAFNOSUPPORT; return -1; }
	return socket(domain, type, protocol);
}

__attribute__((used)) static struct { const void *replacement, *original; } interposers[]
	__attribute__((section("__DATA,__interpose"))) = { { (const void *)noinet6, (const void *)socket } };
#else
#include <dlfcn.h>

int socket(int domain, int type, int protocol)
{
	static int (*real)(int, int, int) = NULL;

	if (domain == AF_INET6) { errno = EAFNOSUPPORT; return -1; }
	if (!real) real = (int (*)(int, int, int))dlsym(RTLD_NEXT, "socket");
	return real(domain, type, protocol);
}
#endif
EOF
"$CC" -shared -fPIC -o "$work/noinet6.so" "$work/noinet6.c" -ldl 2>/dev/null \
	|| "$CC" -shared -fPIC -o "$work/noinet6.so" "$work/noinet6.c" \
	|| skip "cannot build a preloadable library with $CC"

preload() {
	env LD_PRELOAD="$work/noinet6.so" DYLD_INSERT_LIBRARIES="$work/noinet6.so" "$@"
}

# Does preloading it take effect here at all?
printf '%s\n' '#include <sys/types.h>' '#include <sys/socket.h>' '#include <errno.h>' \
	'int main(void) { return (socket(AF_INET6, SOCK_STREAM, 0) == -1) && (errno == EAFNOSUPPORT) ? 0 : 1; }' \
	> "$work/probe.c"
"$CC" -o "$work/probe" "$work/probe.c" || skip "cannot build the preload probe with $CC"
preload "$work/probe" 2>/dev/null || skip "a preloaded library does not replace socket() here"

mkdir -p "$work/home/etc"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
# 2001:db8:: is documentation space; 127.0.0.1 port 22 just has to answer or refuse.
printf '2001:db8::5\tv6host\t# ssh\n127.0.0.1\tv4host\t# ssh\n' > "$work/home/etc/hosts.cfg"

preload env XYMONHOME="$work/home" XYMONNETSVCS=ssh \
	"$XYMONNET" --no-update --noping --timeout=3 >"$work/out" 2>&1 &
pid=$!
register_cleanup "stop_process $pid"

# A correct run takes a few seconds; on main it never ends.
i=0
while kill -0 "$pid" 2>/dev/null; do
	i=$((i + 1))
	if [ "$i" -gt 300 ]; then
		kill "$pid" 2>/dev/null || :
		fail "xymonnet did not finish within 30 s with an IPv6 host and no IPv6 sockets
$(head -5 "$work/out")"
	fi
	sleep 0.1
done
wait "$pid" || :

grep -aqE 'status[^ ]* v6host\.ssh red' "$work/out" \
	|| fail "the IPv6 host's test was not reported red: $(head -20 "$work/out")"
grep -aq 'Service ssh on v6host is not OK' "$work/out" \
	|| fail "the IPv6 host's test gives no reason: $(head -20 "$work/out")"
grep -aqE 'status[^ ]* v4host\.ssh ' "$work/out" && grep -aq 'Service ssh on v4host' "$work/out" \
	|| fail "the IPv4 host in the same run was not tested: $(head -20 "$work/out")"

pass "xymonnet finishes with an IPv6 host on a server without IPv6 sockets, reporting it, and tests the rest"
