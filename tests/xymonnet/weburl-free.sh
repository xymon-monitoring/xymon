#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/weburl-free.sh
#
# decode_url() fills a caller-owned weburl_t with allocations: the two
# urlelem_t blocks and the strings parse_url() strdup's into them, the column
# name, and the post/expect/status data pulled from the spec. xymonnet parses
# a spec once per web test on every run; free_weburl() is what releases that,
# and a field left out of it leaks one block per test per run.
#
# The harness decodes a spread of specs and frees each; the two passes also
# catch a double free. It is checked for leaks whichever tool is at hand:
#
#   - Valgrind, through build/valgrind.sh, when that wrapper and valgrind are
#     both present. Memcheck reports a block definitely or indirectly lost --
#     the indirect case being exactly the weburl's strings -- and exits 99.
#   - AddressSanitizer otherwise, whose LeakSanitizer (the Linux lanes)
#     reports a leak at exit. A sanitizer without leak detection (macOS) runs
#     the harness for a crash/UB check, and the leak part is left unchecked.
#
# So the leak is caught wherever either tool runs, and the test needs neither
# in particular -- build/valgrind.sh (from a separate change) is used if it is
# there, and its absence just falls back to the sanitizer.
#
# Filed under tests/xymonnet, not tests/libxymon: url.o (decode_url,
# free_weburl) is in the server libraries, not the client ones, so a
# client-only build has nothing to link.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

[ -f "$ROOT/lib/url.c" ] || skip "lib/url.c not present in this checkout"

require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")
claim="free_weburl() releases everything decode_url() allocates, with no double free"

compile() {  # compile EXTRA_CFLAGS...
	# shellcheck disable=SC2086  # the flag lists are meant to split
	"$CC" "$@" -g $cflags -o "$work/harness" \
		"$here/weburl-free-harness.c" \
		"$(xymon_lib "$ROOT" libxymoncomm.a)" $ldflags 2>"$work/cc.log" \
		|| { cat "$work/cc.log" >&2; fail "weburl-free harness does not build"; }
}

if [ -x "$ROOT/build/valgrind.sh" ] && command -v valgrind >/dev/null 2>&1; then
	# Valgrind path: a plain build, Memcheck does the checking.
	compile
	rc=0
	VALGRIND_LOG="$work/vg.log" "$ROOT/build/valgrind.sh" "$work/harness" 2>"$work/vg.err" || rc=$?
	case $rc in
		0)  pass "$claim" ;;
		99) sed -n '1,30p' "$work/vg.err" >&2
		    fail "decode_url()/free_weburl() leaks, double-frees or misuses memory" ;;
		77) pass_partial "$claim" "valgrind went missing between the probe and the run" ;;
		*)  sed -n '1,30p' "$work/vg.err" >&2
		    fail "weburl-free harness exited $rc under valgrind" ;;
	esac
else
	# AddressSanitizer fallback. Find out whether this sanitizer can actually
	# see a leak: it links on macOS but carries no LeakSanitizer there, so a
	# clean run would otherwise be read as "no leak" when nothing looked. The
	# probe leaks on purpose; if detect_leaks catches it, the run is a real
	# leak check. detect_leaks on a sanitizer without it aborts at startup
	# with "not supported", so look for the report, not the exit status.
	san=""
	leakcheck=no
	if asan_usable; then
		san="-fsanitize=address,undefined"
		printf '#include <stdlib.h>\nint main(void){char *p = malloc(32); if (p) p[0] = 1; return 0;}\n' >"$work/leak.c"
		if "$CC" -fsanitize=address -g -o "$work/leakprobe" "$work/leak.c" 2>/dev/null; then
			probeout=$(ASAN_OPTIONS=detect_leaks=1 "$work/leakprobe" 2>&1 >/dev/null || true)
			case $probeout in
				*LeakSanitizer*|*"detected memory leaks"*) leakcheck=yes ;;
			esac
		fi
	fi
	asan_opts="detect_leaks=0"
	[ "$leakcheck" = yes ] && asan_opts="detect_leaks=1"

	compile $san

	if ! ASAN_OPTIONS="$asan_opts" "$work/harness" >"$work/out" 2>"$work/err"; then
		sed -n '1,20p' "$work/err" >&2
		fail "decode_url()/free_weburl() leaks, double-frees or faults"
	fi
	if grep -qiE 'LeakSanitizer|detected memory leaks|runtime error' "$work/err"; then
		sed -n '1,20p' "$work/err" >&2
		fail "decode_url()/free_weburl() leaks or misbehaves"
	fi
	if [ "$leakcheck" = yes ]; then
		pass "$claim"
	else
		pass_partial "$claim" "no leak detection here (valgrind absent, sanitizer without LeakSanitizer): the harness ran but leaks were not checked"
	fi
fi
