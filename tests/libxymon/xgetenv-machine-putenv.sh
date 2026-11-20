#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/xgetenv-machine-putenv.sh
#
# xgetenv() derives MACHINE from MACHINEDOTS when MACHINE is unset, dots
# becoming commas. lib/environ.c does it with setenv(), or, where the build
# found no setenv() (build/genconfig.sh leaves HAVE_SETENV undefined), with
# putenv() on a buffer it allocates. That branch called SBUF_MALLOC with the
# size alone, without the buffer, so lib/ did not compile there at all.
#
# No platform the lanes run lacks setenv(), so the branch is reached by
# compiling lib/environ.c with a copy of include/config.h whose HAVE_SETENV is
# undefined, loaded first with -include: its include guard (__CONFIG_H__) then
# keeps the tree's own copy out, which libxymon.h would otherwise find beside
# it whatever the search path. The harness then asks
# for MACHINE with MACHINEDOTS=mx.example.com, and gets mx,example,com.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymonclient.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymonclient.a

# include/config.h with HAVE_SETENV undefined.
mkdir -p "$work/nosetenv"
sed 's/^#define HAVE_SETENV.*/#undef HAVE_SETENV/' "$ROOT/include/config.h" >"$work/nosetenv/config.h"
grep -q '^#undef HAVE_SETENV' "$work/nosetenv/config.h" \
	|| fail "include/config.h carries no HAVE_SETENV line to undefine, so the branch without setenv() cannot be reached this way"

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086 # the flag strings are intentionally word-split
"$CC" -include "$work/nosetenv/config.h" $harness_cflags \
	-DXYMONBUILD='""' -DXYMONTOPDIR='"/t"' -DXYMONLOGDIR='"/t/log"' -DXYMONHOSTNAME='"h"' \
	-DXYMONHOSTIP='"127.0.0.1"' -DXYMONHOSTOS='"linux"' -DXYMONHOME='"/t"' \
	-o "$work/harness" "$here/xgetenv-machine-putenv-harness.c" "$ROOT/lib/environ.c" \
	"$(xymon_lib "$ROOT" libxymonclient.a)" $harness_ldflags 2>"$work/cc.log" \
	|| fail "lib/environ.c does not compile without setenv(): $(cat "$work/cc.log")"

got=$(env -u MACHINE MACHINEDOTS=mx.example.com "$work/harness")
[ "$got" = "mx,example,com" ] \
	|| fail "without setenv(), xgetenv(\"MACHINE\") with MACHINEDOTS=mx.example.com gave '$got', not 'mx,example,com'"

pass "without setenv(), xgetenv() derives MACHINE from MACHINEDOTS, dots as commas"
