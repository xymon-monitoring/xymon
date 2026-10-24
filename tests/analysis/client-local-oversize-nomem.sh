#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/analysis/client-local-oversize-nomem.sh
#
# The other half of client-local-oversize.sh: when the buffer cannot grow,
# "xymond_client --local" must fall back to what it did before #681 --
# truncate the report at MAXMSG_CLIENT -- and keep reading the stream.
#
# A preloaded realloc() refuses the one size the first growth asks for
# (64 KB doubled, plus the reader's 4 KB of extra room and the NUL byte),
# so nothing else in xymond_client is affected. Checked:
#
#   - the refusal happened, so the test exercised the fallback at all
#   - the report is truncated, as before the fix: the over-size error is
#     logged and the [df] section after the cut is lost
#   - the message after it is still read
#
# LD_PRELOAD as used here is a Linux interface; elsewhere this skips.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

[ "$(uname -s)" = Linux ] || skip "preloading a failing realloc() is only set up for Linux"
require_cc
require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
: > "$work/analysis.cfg"

cat > "$work/failrealloc.c" <<'EOF'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

void *realloc(void *p, size_t n)
{
	static void *(*real)(void *, size_t) = NULL;
	const char *fail = getenv("FAIL_REALLOC_SIZE");

	if (fail && (n == (size_t)strtoul(fail, NULL, 10))) {
		static const char msg[] = "failrealloc: refused\n";
		write(2, msg, sizeof(msg) - 1);
		return NULL;
	}
	if (!real) real = (void *(*)(void *, size_t))dlsym(RTLD_NEXT, "realloc");
	return real(p, n);
}
EOF
"$CC" -shared -fPIC -o "$work/failrealloc.so" "$work/failrealloc.c" -ldl \
	|| skip "cannot build a preload library with $CC"

{
	printf '%s\n' '@@client|1755640000|127.0.0.1|big.test|linux|linux|' '[ps]' \
		'PID PPID USER STARTED S PRI %CPU TIME %MEM RSZ VSZ CMD'
	i=0
	while [ $i -lt 2500 ]; do
		printf '%d 1 root 10:00 S 20 0.0 00:00:00 0.0 1000 2000 /usr/sbin/daemon --instance %d --padding xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n' $((i + 100)) $i
		i=$((i + 1))
	done
	printf '%s\n' '[df]' 'Filesystem 1024-blocks Used Available Capacity Mounted on' \
		'/dev/sdb1 1000000 1000000 0 100% /full' '@@'
	printf '%s\n' '@@client|1755640000|127.0.0.1|small.test|linux|linux|' '[df]' \
		'Filesystem 1024-blocks Used Available Capacity Mounted on' \
		'/dev/sda1 1000000 400000 600000 40% /' '@@'
} > "$work/msgs"

# 64 KB doubled, + EXTRABUFSPACE (4095) + 1 for the NUL byte.
out=$(env \
	XYMONHOME="$work/home" \
	XYMONTMP="$work" \
	MAXMSG_CLIENT=64 \
	FAIL_REALLOC_SIZE=$((2 * 64 * 1024 + 4095 + 1)) \
	LD_PRELOAD="$work/failrealloc.so" \
	"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
	<"$work/msgs" 2>"$work/stderr.log") || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"

errs=$(cat "$work/stderr.log")
assert_contains "failrealloc: refused" "$errs" \
	"the preloaded realloc() never refused, so the fallback was not exercised"
assert_contains "over-size" "$errs" \
	"with the buffer unable to grow, the report was not truncated as before"
assert_not_contains "/full (100% used)" "$out" \
	"the [df] section after the cut was reported although the buffer could not grow"
assert_match "small,test\.disk green" "$out" \
	"after the fallback, the next message on the stream was not read"

pass "xymond_client --local truncates as before, and keeps reading, when its buffer cannot grow"
