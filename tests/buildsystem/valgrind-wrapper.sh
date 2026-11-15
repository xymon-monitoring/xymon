#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/valgrind-wrapper.sh
#
# Pins what build/valgrind.sh promises a caller: without valgrind it says so
# and exits 77; with it, a program that leaks a block makes it exit 99 and
# its report says what was lost, a program that frees everything keeps its
# own exit status, and VALGRIND_LOG chooses the report's file. Where valgrind
# is not installed only the first part can run, and the test says so.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
VG="$ROOT/build/valgrind.sh"
SH=$(command -v sh)

work=$(mktempdir)
register_cleanup "rm -rf '$work'"

# Without valgrind on PATH. Only shell builtins run before the script
# gives up, so an empty PATH is enough.
rc=0
PATH="$work/empty" "$SH" "$VG" true 2>"$work/novg.err" || rc=$?
assert_equal 77 "$rc" "build/valgrind.sh without valgrind exits 77"
assert_contains "valgrind is not installed" "$(cat "$work/novg.err")" \
	"build/valgrind.sh without valgrind says why"

if ! command -v valgrind >/dev/null 2>&1; then
	pass_partial "build/valgrind.sh exits 77, saying why, when valgrind is missing" \
		"valgrind is not installed here: the leak detection was not run"
fi

require_cc
cat >"$work/leak.c" <<'EOF'
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	char *p = malloc(10);
	memset(p, 0, 10);
	p = NULL;	/* the block is lost */
	return (argc > 1) ? atoi(argv[1]) : 0;
}
EOF
cat >"$work/clean.c" <<'EOF'
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	char *p = malloc(10);
	memset(p, 0, 10);
	free(p);
	return (argc > 1) ? atoi(argv[1]) : 0;
}
EOF
for prog in leak clean; do
	"$CC" -g -O0 -o "$work/$prog" "$work/$prog.c" 2>"$work/cc.log" \
		|| { cat "$work/cc.log" >&2; fail "$prog.c does not compile"; }
done

rc=0
VALGRIND_LOG="$work/leak.vg" "$SH" "$VG" "$work/leak" 2>"$work/leak.err" || rc=$?
assert_equal 99 "$rc" "build/valgrind.sh on a program that leaks exits 99"
assert_file_exists "$work/leak.vg" "the report goes to VALGRIND_LOG"
assert_contains "definitely lost: 10 bytes in 1 blocks" "$(cat "$work/leak.err")" \
	"the summary names the lost block"

rc=0
VALGRIND_LOG="$work/clean.vg" "$SH" "$VG" "$work/clean" 3 2>"$work/clean.err" || rc=$?
assert_equal 3 "$rc" "build/valgrind.sh on a program that frees everything keeps its exit status"
assert_contains "ERROR SUMMARY: 0 errors" "$(cat "$work/clean.err")" \
	"the summary reports no error"

pass "build/valgrind.sh exits 99 on a leak, keeps a clean program's status, and 77 without valgrind"
