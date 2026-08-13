#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/client-sections-expiry.sh
#
# xymond logs when a collector stops sending a report section (#201), so the
# columns it fed go purple while the host keeps reporting. The diagnostic is
# only meant for a section that disappeared between two consecutive reports of
# the same collector -- a collector whose entry was purged for being idle
# longer than MAX_SUBCLIENT_LIFETIME must come back as a fresh baseline and say
# nothing.
#
# That did not hold. The entry's timestamp was refreshed before the comparison
# and before the purge, so a returning collector could no longer be recognised
# as expired: its stale section list was compared against the new report and
# every section missing from it was announced as vanished.
#
# clientmsg_refresh() and update_clientsections() are static and xymond has no
# library form, so the real ones are extracted from xymond.c into a generated
# translation unit and driven with timestamps of the test's choosing -- waiting
# out MAX_SUBCLIENT_LIFETIME (960s) is not something a test can do.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")
XYMOND_C="$ROOT/xymond/xymond.c"
[ -f "$XYMOND_C" ] || skip "xymond/xymond.c not present in this checkout"

require_cc
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not configured/built (need include/config.h and lib/libxymoncomm.a)"

work=$(mktempdir)

# The real definitions, not copies: a copy keeps passing after production
# stops behaving the way it describes.
{
	awk '/^#define MAX_SUBCLIENT_LIFETIME/' "$XYMOND_C"
	awk '/^static void clientmsg_refresh/,/^}/'     "$XYMOND_C"
	awk '/^static void update_clientsections/,/^}/' "$XYMOND_C"
} >"$work/extracted.inc"

grep -q 'MAX_SUBCLIENT_LIFETIME'   "$work/extracted.inc" || fail "could not extract MAX_SUBCLIENT_LIFETIME from xymond.c"
grep -q 'clientmsg_refresh'        "$work/extracted.inc" || fail "could not extract clientmsg_refresh() from xymond.c"
grep -q 'update_clientsections'    "$work/extracted.inc" || fail "could not extract update_clientsections() from xymond.c"

# The configured link flags, not a hand-picked SSLLIBS: they carry the library
# search path and the runtime path as well. Without the rpath the harness links
# on NetBSD and then cannot run - "Shared object libpcre2-8.so.0 not found",
# since pkgsrc puts it under /usr/pkg/lib.
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
pcre_libs=${PCRELIBS:-$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# shellcheck disable=SC2086
"$CC" $harness_cflags -I"$work" -o "$work/harness" \
	"$here/client-sections-expiry-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# run_scenario <name> -- run the harness and set NWARN to the number of
# vanished-section lines it logged.
#
# Not a function whose value is taken with $(...): fail() inside a command
# substitution ends the subshell, not the test, and the caller then compares
# against an empty string with no idea why. The run is also checked apart from
# the count -- piping the harness straight into grep -c hid every way it can
# fail to run at all, and a loader that cannot find libpcre2 exits nonzero
# while the count still comes back 0, which is the very answer the first
# scenario expects. The test passed without the code under test executing once.
run_scenario() {
	local out rc
	set +e
	out=$("$work/harness" "$1" 2>&1 >/dev/null)
	rc=$?
	set -e
	[ "$rc" -eq 0 ] || fail "the harness did not run scenario '$1' (exit $rc): $out"
	NWARN=$(printf '%s\n' "$out" | grep -c 'no longer sends section' || true)
}

# A collector idle for longer than MAX_SUBCLIENT_LIFETIME comes back without a
# section it used to send. Its entry would have been purged in between, so the
# returning report is a new baseline and nothing vanished.
run_scenario expired
assert_equal "0" "$NWARN" \
	"a collector returning after its entry expired was reported as having dropped a section"

# The same removal between two reports close together is a real change, and
# must still be reported - exactly once, not once per section that happens to
# be absent.
run_scenario live
assert_equal "1" "$NWARN" \
	"a section removed between two live reports must be reported exactly once"

# And the returning report really does become the baseline: a removal after it
# is reported again. Without this, "no warning after expiry" could be had by
# never comparing again at all.
run_scenario expired-then-live
assert_equal "1" "$NWARN" \
	"after a collector returned from expiry, a later removal must still be reported"

pass "a collector returning from expiry starts a new baseline; live removals are still reported once"
