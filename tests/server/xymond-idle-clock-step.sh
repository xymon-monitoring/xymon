#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-idle-clock-step.sh
#
# A step of the wall clock neither drops nor keeps a connection: xymond
# times a connection's --timeout and its IDLE_FIRSTBYTE on the monotonic
# clock. On the wall clock, a forward step made every connection that had
# sent nothing look expired, and xymond dropped them all at once -- the
# xymond-idle-connections.sh flake on a WSL2 host whose clock is stepped
# forward about 3 seconds every 35.
#
# Checked, with xymond-idle-connections-harness.c:
#   - three connections that send nothing are still open 2 seconds after
#     the wall clock jumped 60 seconds ahead;
#   - they are closed by the time 7 seconds have passed: the 5 seconds a
#     silent connection gets still hold, on the monotonic clock.
#
# The step comes from a library injected into xymond that replaces time(),
# with LD_PRELOAD, or DYLD_INSERT_LIBRARIES and an interpose table on macOS;
# both are set, and each system ignores the other. It reads its settings
# once, when it loads, and writes a marker then: where xymond does not load
# it (a static build), the marker is missing and the test skips.
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
printf 'page test Test\n127.0.0.1 testhost # conn\n' > "$work/hosts.cfg"
"$CC" -o "$work/harness" "$(dirname "$0")/xymond-idle-connections-harness.c" 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"

cat > "$work/stepclock.c" <<'EOF'
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

static char trigger[1024];

__attribute__((constructor)) static void stepclock_init(void)
{
	const char *t = getenv("STEPCLOCK_TRIGGER"), *m = getenv("STEPCLOCK_MARKER");
	int fd;

	if (t) strncpy(trigger, t, sizeof(trigger) - 1);
	if (m && ((fd = open(m, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0)) close(fd);
}

/* 60 seconds ahead once the trigger file exists */
static time_t stepped(time_t t)
{
	return (*trigger && (access(trigger, F_OK) == 0)) ? t + 60 : t;
}

#ifdef __APPLE__
static time_t steptime(time_t *p)
{
	time_t t = stepped(time(NULL));

	if (p) *p = t;
	return t;
}

__attribute__((used)) static struct { const void *replacement, *original; } interposers[]
	__attribute__((section("__DATA,__interpose"))) = { { (const void *)steptime, (const void *)time } };
#else
#include <dlfcn.h>

time_t time(time_t *p)
{
	static time_t (*real)(time_t *) = NULL;
	time_t t;

	if (!real) real = (time_t (*)(time_t *))dlsym(RTLD_NEXT, "time");
	t = stepped(real(NULL));
	if (p) *p = t;
	return t;
}
#endif
EOF
"$CC" -shared -fPIC -o "$work/stepclock.so" "$work/stepclock.c" -ldl 2>/dev/null \
	|| "$CC" -shared -fPIC -o "$work/stepclock.so" "$work/stepclock.c" \
	|| skip "cannot build an injectable library with $CC"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

P=$(free_port)
env STEPCLOCK_TRIGGER="$work/step" STEPCLOCK_MARKER="$work/loaded" \
	LD_PRELOAD="$work/stepclock.so" DYLD_INSERT_LIBRARIES="$work/stepclock.so" \
	"$XYMOND" --no-daemon --listen="127.0.0.1:$P" \
	--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
	> "$work/xymond.log" 2>&1 &
pid=$!
register_cleanup "stop_process $pid"
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && break
	kill -0 "$pid" 2>/dev/null || fail "xymond did not start: $(cat "$work/xymond.log")"
	sleep 0.1
done
grep -q 'Setup complete' "$work/xymond.log" || fail "xymond did not finish starting: $(cat "$work/xymond.log")"
[ -f "$work/loaded" ] || skip "xymond does not load an injected library here, so the clock cannot be stepped under it"

# Three silent connections; the clock jumps ahead after a second, and they
# are counted 2 seconds after that, well inside their 5 seconds.
"$work/harness" count 127.0.0.1 "$P" 3 3 > "$work/count.out" 2>&1 &
cpid=$!
sleep 1
: > "$work/step"
wait "$cpid" || fail "the harness failed counting silent connections: $(cat "$work/count.out")"
assert_contains "open 3" "$(cat "$work/count.out")" \
	"silent connections were dropped when the wall clock jumped ahead, 3 seconds after they opened; xymond's log: $(cat "$work/xymond.log")"

# The clock stays stepped: the 5 seconds still run out.
got=$("$work/harness" count 127.0.0.1 "$P" 3 7 2>&1) || fail "the harness failed counting silent connections: $got"
assert_contains "open 0" "$got" \
	"silent connections were not closed within 7 seconds after the wall clock jumped ahead; xymond's log: $(cat "$work/xymond.log")"

pass "a step of the wall clock neither drops a silent connection early nor keeps it past its 5 seconds"
