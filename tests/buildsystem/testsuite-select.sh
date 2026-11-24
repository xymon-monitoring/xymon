#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/testsuite-select.sh
#
# A command-line selector runs only the tests it picks, under the same per-test
# watchdog as a full run; a selector that matches nothing is refused, and a
# selector cannot be combined with XYMON_TESTS_STRICT.
#
# Running one test straight -- ./tests/<area>/<t>.sh -- has no runner and no
# watchdog, so a test that hangs there leaks a backgrounded child (a probe that
# never returned saturated a core for an hour once, orphaned to init). A
# selector gives the focused run the watchdog back, so this checks both halves:
# the right tests run, and a picked test that hangs is still stopped.
#
# Against fake trees, as testsuite-timeout.sh does: a real area would tie this
# to the calling run's variant and coverage floor.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir)
unset XYMON_TESTS_STRICT XYMON_VARIANT XYMON_TESTS_TIMEOUT

# Whether the runner can reach a backgrounded grandchild to stop it: ps, or
# /proc where ps is absent. The one host with neither reports that row partial.
reap_ok=0
if command -v ps >/dev/null 2>&1 || [ -r /proc/1/task/1/children ]; then
	reap_ok=1
fi

# maketree DIR -- a fake suite root with this runner and three tests in two
# areas, each of which records that it ran.
maketree() {
	mkdir -p "$1/tests/alpha" "$1/tests/beta" "$1/tmp"
	cp "$ROOT/tests/testsuite" "$1/tests/testsuite"
	chmod +x "$1/tests/testsuite"
	for spec in alpha/1-aaa beta/1-bbb beta/2-ccc; do
		printf '#!/usr/bin/env bash\n: > "%s/ran-%s"\n' "$1" "${spec#*/}" \
			> "$1/tests/$spec.sh"
		chmod +x "$1/tests/$spec.sh"
	done
}

# runsel DIR LOG LIMIT [ENV=VAL ...] -- [SELECTOR ...] after a lone '--'
# run DIR's suite and wait at most LIMIT seconds. Sets rc to its status, or
# "hung" when it outlasted LIMIT. Any VAR=VAL before '--' is set for the run.
runsel() {
	local d=$1 log=$2 limit=$3 env='' pid i=0; shift 3
	while [ "$#" -gt 0 ] && [ "$1" != -- ]; do env="$env $1"; shift; done
	[ "${1:-}" = -- ] && shift
	( cd "$d" && eval "TMPDIR=\"$d/tmp\" $env" exec ./tests/testsuite "$@" >"$log" 2>&1 ) &
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

# --- an area glob runs that area's tests and no others ------------------------
d="$work/area"; maketree "$d"
runsel "$d" "$work/area.log" 60 -- 'beta/*'
[ "$rc" = 0 ] || fail "a 'beta/*' selector did not pass (rc=$rc): $(cat "$work/area.log")"
[ -f "$d/ran-1-bbb" ] || fail "'beta/*' did not run beta/1-bbb: $(cat "$work/area.log")"
[ -f "$d/ran-2-ccc" ] || fail "'beta/*' did not run beta/2-ccc: $(cat "$work/area.log")"
[ ! -f "$d/ran-1-aaa" ] || fail "'beta/*' also ran alpha/1-aaa, which it does not pick"

# --- a stem selector runs exactly that one test -------------------------------
d="$work/stem"; maketree "$d"
runsel "$d" "$work/stem.log" 60 -- 1-aaa
[ "$rc" = 0 ] || fail "a '1-aaa' selector did not pass (rc=$rc): $(cat "$work/stem.log")"
[ -f "$d/ran-1-aaa" ] || fail "'1-aaa' did not run alpha/1-aaa: $(cat "$work/stem.log")"
[ ! -f "$d/ran-1-bbb" ] || fail "'1-aaa' also ran beta/1-bbb -- a selector must not run what it does not pick"
[ ! -f "$d/ran-2-ccc" ] || fail "'1-aaa' also ran beta/2-ccc -- a selector must not run what it does not pick"

# --- a basename glob picks by file name --------------------------------------
d="$work/base"; maketree "$d"
runsel "$d" "$work/base.log" 60 -- '2-ccc.sh'
[ -f "$d/ran-2-ccc" ] || fail "'2-ccc.sh' did not run beta/2-ccc: $(cat "$work/base.log")"
[ ! -f "$d/ran-1-aaa" ] || fail "'2-ccc.sh' also ran alpha/1-aaa"
[ ! -f "$d/ran-1-bbb" ] || fail "'2-ccc.sh' also ran beta/1-bbb"

# --- a selector that matches nothing is refused, and runs nothing -------------
d="$work/none"; maketree "$d"
runsel "$d" "$work/none.log" 60 -- 'gamma/*'
[ "$rc" = 1 ] || fail "a selector matching nothing was not refused (rc=$rc): $(cat "$work/none.log")"
assert_contains "no test matches: gamma/*" "$(cat "$work/none.log")" \
	"the refusal does not name the selector that matched nothing"
[ ! -f "$d/ran-1-aaa" ] && [ ! -f "$d/ran-1-bbb" ] && [ ! -f "$d/ran-2-ccc" ] \
	|| fail "a selector matching nothing still ran a test"

# --- a selector with XYMON_TESTS_STRICT=1 is refused, before any test ---------
d="$work/strict"; maketree "$d"
runsel "$d" "$work/strict.log" 60 XYMON_TESTS_STRICT=1 -- 'beta/*'
[ "$rc" = 1 ] || fail "a selector with XYMON_TESTS_STRICT=1 was not refused (rc=$rc): $(cat "$work/strict.log")"
assert_contains "cannot be combined with XYMON_TESTS_STRICT=1" "$(cat "$work/strict.log")" \
	"the refusal does not say why a selector and the strict floor do not combine"
[ ! -f "$d/ran-1-bbb" ] || fail "a test ran although the selector+strict combination was refused"

# --- a picked test that hangs is stopped: selecting keeps the watchdog --------
d="$work/hang"; maketree "$d"
cat > "$d/tests/beta/9-hang.sh" <<EOF
#!/usr/bin/env bash
trap ': > "$d/trap"' EXIT
sleep 600 &
echo \$! > "$d/child"
: > "$d/started"
while [ ! -f "$d/release" ]; do sleep 1; done
EOF
chmod +x "$d/tests/beta/9-hang.sh"
runsel "$d" "$work/hang.log" 60 XYMON_TESTS_TIMEOUT=2 -- 9-hang
[ -s "$d/child" ] && register_cleanup "stop_process $(cat "$d/child")"
[ "$rc" != hung ] || fail "selecting a hanging test, the runner did not stop it in 60s: $(cat "$work/hang.log")"
[ "$rc" -ne 0 ] || fail "a run whose only, picked, test was stopped for time reported success: $(cat "$work/hang.log")"
assert_contains "tests/beta/9-hang.sh timed out after 2s" "$(cat "$work/hang.log")" \
	"a picked test that hung was not reported timed out -- selecting lost the watchdog"
[ ! -f "$d/ran-1-bbb" ] || fail "selecting 9-hang also ran beta/1-bbb"
child=$(cat "$d/child")
if process_running "$child"; then
	if [ "$reap_ok" = 1 ]; then
		fail "the picked hung test's child, pid $child, was left running -- the orphan a selector is meant to prevent"
	else
		child_unreaped=1
	fi
fi

msg="a selector runs the tests it picks and no others, refuses an empty match and XYMON_TESTS_STRICT, and keeps the watchdog so a picked test that hangs is stopped"
if [ "${child_unreaped:-0}" = 1 ]; then
	pass_partial "$msg" \
		"this host has neither ps nor /proc, so the runner could not reach the child the picked hung test backgrounded; its reaping is unverified here"
fi
pass "$msg"
