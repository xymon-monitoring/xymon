#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/testsuite-timeout.sh
#
# A test that outlasts XYMON_TESTS_TIMEOUT is stopped, reported as such, and
# the run goes on.
#
# A test that never ended held the whole run until the CI step's own timeout:
# the tests after it got no result, and the log said only that the action had
# timed out, naming no test (the OpenBSD 7.9 lane, in
# tests/client/msgcache-failed-pull.sh). The runner now gives each test
# XYMON_TESTS_TIMEOUT seconds, 600 unless set, 0 for no limit.
#
# Against fake trees, as testsuite-run-disposal.sh does. A hung test is one
# that starts a child and then blocks in another, as a test waiting on a peer
# does: stopping the test's shell alone leaves its children running, so the
# child is checked too, and the test's EXIT trap, where its cleanups run. The
# runner reaches that child through ps, or through /proc where ps is absent, as
# on the smallest container images; where a host has neither, that one check is
# reported partial.
#
# Each run here is bounded by this test, not by the runner under test: a
# runner that did not stop the hung test would otherwise hang this one too.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir)
# The fake trees have an area of their own, which the calling run's variant and
# coverage floor know nothing of (testsuite-run-disposal.sh says why).
unset XYMON_TESTS_STRICT XYMON_VARIANT XYMON_TESTS_TIMEOUT

# The runner stops a timed-out test by signalling what it finds descended from
# it, read from ps or, where ps is absent, from /proc. A host with neither
# cannot reach a child the test backgrounded, so the row that checks the child
# was stopped is reported partial there, not failed.
reap_ok=0
if command -v ps >/dev/null 2>&1 || [ -r /proc/1/task/1/children ]; then
	reap_ok=1
fi

# maketree DIR -- a fake suite root with this runner and no test yet.
maketree() {
	mkdir -p "$1/tests/dummy" "$1/tmp"
	cp "$ROOT/tests/testsuite" "$1/tests/testsuite"
	chmod +x "$1/tests/testsuite"
}

# hangtest DIR -- tests/dummy/1-hang.sh: records its EXIT trap in DIR/trap,
# starts a child whose pid goes in DIR/child, then blocks until DIR/release.
hangtest() {
	cat > "$1/tests/dummy/1-hang.sh" <<EOF
#!/usr/bin/env bash
trap ': > "$1/trap"' EXIT
sleep 600 &
echo \$! > "$1/child"
: > "$1/started"
while [ ! -f "$1/release" ]; do sleep 1; done
EOF
	chmod +x "$1/tests/dummy/1-hang.sh"
}

# runsuite DIR LOG LIMIT [TIMEOUT] -- run DIR's suite with XYMON_TESTS_TIMEOUT
# set to TIMEOUT, unset if absent, and wait at most LIMIT seconds for it. Sets
# rc to its exit status, or to "hung" when it outlasted LIMIT and was killed.
runsuite() {
	local d=$1 log=$2 limit=$3 pid i=0
	if [ $# -ge 4 ]; then
		( cd "$d" && TMPDIR="$d/tmp" XYMON_TESTS_TIMEOUT=$4 exec ./tests/testsuite >"$log" 2>&1 ) &
	else
		( cd "$d" && TMPDIR="$d/tmp" exec ./tests/testsuite >"$log" 2>&1 ) &
	fi
	pid=$!
	while [ "$i" -lt "$limit" ] && kill -0 "$pid" 2>/dev/null; do sleep 1; i=$((i + 1)); done
	if kill -0 "$pid" 2>/dev/null; then
		: > "$d/release"
		kill -KILL "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
		rc=hung
	else
		set +e; wait "$pid"; rc=$?; set -e
	fi
}

# --- a test that hangs is stopped, and the next one still runs ----------------
d="$work/hang"; maketree "$d"; hangtest "$d"
printf '#!/usr/bin/env bash\n: > "%s/second"\n' "$d" > "$d/tests/dummy/2-pass.sh"
chmod +x "$d/tests/dummy/2-pass.sh"
runsuite "$d" "$work/hang.log" 60 2
[ -s "$d/child" ] && register_cleanup "stop_process $(cat "$d/child")"
[ "$rc" != hung ] || fail "the runner did not stop a test that outlasted XYMON_TESTS_TIMEOUT=2 in 60 seconds: $(cat "$work/hang.log")"
[ "$rc" -ne 0 ] || fail "a run whose test was stopped for its time reported success: $(cat "$work/hang.log")"
assert_contains "tests/dummy/1-hang.sh timed out after 2s" "$(cat "$work/hang.log")" \
	"the stopped test was not reported as timed out"
[ -f "$d/second" ] || fail "the test after the stopped one did not run: $(cat "$work/hang.log")"
[ -f "$d/trap" ] || fail "the stopped test's EXIT trap, where its cleanups run, did not run"
child=$(cat "$d/child")
if process_running "$child"; then
	if [ "$reap_ok" = 1 ]; then
		fail "the stopped test's child, pid $child, was left running"
	else
		child_unreaped=1
	fi
fi

# --- 0 lifts the limit: a test longer than 0 seconds is not stopped ------------
d="$work/nolimit"; maketree "$d"
printf '#!/usr/bin/env bash\nsleep 3\n' > "$d/tests/dummy/1-slow.sh"
chmod +x "$d/tests/dummy/1-slow.sh"
runsuite "$d" "$work/nolimit.log" 60 0
[ "$rc" = 0 ] || fail "with XYMON_TESTS_TIMEOUT=0, a 3-second test did not pass (rc=$rc): $(cat "$work/nolimit.log")"

# --- a value that is not a number of seconds is refused, before any test ------
d="$work/bad"; maketree "$d"
printf '#!/usr/bin/env bash\n: > "%s/ran"\n' "$d" > "$d/tests/dummy/1-any.sh"
chmod +x "$d/tests/dummy/1-any.sh"
runsuite "$d" "$work/bad.log" 60 10m
[ "$rc" = 1 ] || fail "XYMON_TESTS_TIMEOUT=10m was not refused (rc=$rc): $(cat "$work/bad.log")"
assert_contains "XYMON_TESTS_TIMEOUT='10m' is not a number of seconds" "$(cat "$work/bad.log")" \
	"the refusal does not name the value"
[ ! -f "$d/ran" ] || fail "a test ran although XYMON_TESTS_TIMEOUT was refused"

# --- TERM to the run reaches the test running in the background ---------------
d="$work/term"; maketree "$d"; hangtest "$d"
( cd "$d" && TMPDIR="$d/tmp" exec ./tests/testsuite >"$work/term.log" 2>&1 ) &
runner=$!
i=0
while [ "$i" -lt 30 ] && [ ! -f "$d/started" ]; do sleep 1; i=$((i + 1)); done
[ -f "$d/started" ] || { kill -KILL "$runner" 2>/dev/null; fail "the hung test never started under the runner"; }
[ -s "$d/child" ] && register_cleanup "stop_process $(cat "$d/child")"
kill -TERM "$runner"
i=0
while [ "$i" -lt 30 ] && [ ! -f "$d/trap" ]; do sleep 1; i=$((i + 1)); done
if [ ! -f "$d/trap" ]; then
	: > "$d/release"
	fail "TERM to the run did not reach the test running in the background: its EXIT trap did not run"
fi
wait "$runner" 2>/dev/null || true

if [ "${child_unreaped:-0}" = 1 ]; then
	pass_partial \
		"a test outlasting XYMON_TESTS_TIMEOUT is stopped with its child and reported, the run goes on; 0 lifts the limit, a bad value is refused, and TERM reaches the test" \
		"this host has neither ps nor /proc, so the runner could not reach the child the hung test backgrounded and its cleanup is unverified here"
fi
pass "a test outlasting XYMON_TESTS_TIMEOUT is stopped with its child and reported, the run goes on; 0 lifts the limit, a bad value is refused, and TERM reaches the test"
