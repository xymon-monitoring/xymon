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

harness_cflags=$(xymon_cflags "$ROOT")
pcre_libs=${PCRELIBS:-$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

# shellcheck disable=SC2086
"$CC" $harness_cflags -I"$work" -o "$work/harness" \
	"$here/client-sections-expiry-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

warnings() {  # warnings <scenario> -- how many vanished-section lines it logged
	"$work/harness" "$1" 2>&1 >/dev/null | grep -c 'no longer sends section' || true
}

# A collector idle for longer than MAX_SUBCLIENT_LIFETIME comes back without a
# section it used to send. Its entry would have been purged in between, so the
# returning report is a new baseline and nothing vanished.
assert_equal "0" "$(warnings expired)" \
	"a collector returning after its entry expired was reported as having dropped a section"

# The same removal between two reports close together is a real change, and
# must still be reported - exactly once, not once per section that happens to
# be absent.
assert_equal "1" "$(warnings live)" \
	"a section removed between two live reports must be reported exactly once"

# And the returning report really does become the baseline: a removal after it
# is reported again. Without this, "no warning after expiry" could be had by
# never comparing again at all.
assert_equal "1" "$(warnings expired-then-live)" \
	"after a collector returned from expiry, a later removal must still be reported"

pass "a collector returning from expiry starts a new baseline; live removals are still reported once"
