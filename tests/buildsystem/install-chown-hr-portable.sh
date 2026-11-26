#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-chown-hr-portable.sh
#
# chown and chgrp on the BSDs reject -h together with -R ("the -R and -h
# options may not be specified together"), so "chown -hR" in an install
# recipe aborts the install there. Recurse with find instead:
#   find DIR -exec chown -h USER {} \;
# This reads the install makefiles statically, so it runs on any platform and
# needs no build.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

bad=""
for f in build/Makefile.rules client/Makefile common/Makefile xymond/Makefile; do
	[ -f "$ROOT/$f" ] || continue
	hits=$(sed "s/#.*//" "$ROOT/$f" \
		| grep -nE "(chown|chgrp)[[:space:]]+-[A-Za-z]*(hR|Rh|h[A-Za-z]*R|R[A-Za-z]*h)" \
		| grep . || true)
	[ -z "$hits" ] || bad="$bad
$f:
$hits"
done

[ -z "$bad" ] || fail "an install makefile combines -h and -R on chown/chgrp, which the BSDs reject; recurse with find ... -exec chown -h instead:$bad"

pass "no install makefile combines -h and -R on chown/chgrp (the BSDs reject that)"
