#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/rrd-api-compat.sh
#
# Regression guard for lib/rrd_api_compat.h (PR #80).
#
# RRDtool changed the argv parameter of its public API from "char **" to
# "const char **". lib/rrd_api_compat.h hides that behind xymon_rrd_argv_item_t
# and the xymon_rrd_* wrappers, selected by RRD_CONST_ARGS (probed by
# build/rrd.sh).
#
# We compile a small probe that includes the header and calls every wrapper,
# against a mocked <rrd.h> in BOTH the legacy ("char **") and the modern
# ("const char **") forms -- with the matching RRD_CONST_ARGS value. No real
# librrd is needed, so it runs anywhere with a C compiler. Skips cleanly when
# the compiler or the header is absent (e.g. a branch predating PR #80).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
HEADER="$ROOT/lib/rrd_api_compat.h"
CC="${CC:-cc}"

command -v "$CC" >/dev/null 2>&1 || skip "no C compiler (CC=$CC)"
[ -f "$HEADER" ] || skip "lib/rrd_api_compat.h absent (predates PR #80)"

WORK=$(mktempdir)

write_mock_rrd_h() {            # $1 = legacy | const
	local argv="char **"
	[ "$1" = "const" ] && argv="const char **"
	cat >"$WORK/rrd.h" <<EOF
#ifndef RRD_H
#define RRD_H
#include <time.h>
typedef double rrd_value_t;
int rrd_update(int, $argv);
int rrd_create(int, $argv);
int rrd_fetch(int, $argv, time_t *, time_t *, unsigned long *,
              unsigned long *, char ***, rrd_value_t **);
int rrd_graph(int, $argv, char ***, int *, int *, void *, double *, double *);
#endif
EOF
}

cat >"$WORK/probe.c" <<'EOF'
#include "rrd_api_compat.h"

int main(void)
{
	xymon_rrd_argv_item_t argv[2] = { "x", 0 };
	char **calcpr = 0, **dsnames = 0;
	rrd_value_t *data = 0;
	int xsize = 0, ysize = 0;
	double ymin = 0, ymax = 0;
	time_t start = 0, end = 0;
	unsigned long step = 0, dscount = 0;

	(void)xymon_rrd_update(1, argv);
	(void)xymon_rrd_create(1, argv);
	(void)xymon_rrd_fetch(1, argv, &start, &end, &step, &dscount,
			      &dsnames, &data);
	(void)xymon_rrd_graph(1, argv, &calcpr, &xsize, &ysize, 0,
			      &ymin, &ymax);
	return 0;
}
EOF

check() {                      # $1 = legacy|const   $2 = RRD_CONST_ARGS value
	write_mock_rrd_h "$1"
	printf '  %-7s API (RRD_CONST_ARGS=%s) ... ' "$1" "$2"
	if ! "$CC" -std=c99 -Wall -Wextra -Werror "-DRRD_CONST_ARGS=$2" \
		-I"$WORK" -I"$ROOT/lib" \
		-c "$WORK/probe.c" -o "$WORK/probe.o" 2>"$WORK/err"; then
		echo "FAILED"
		fail "rrd_api_compat.h does not compile for the $1 argv API:
$(cat "$WORK/err")"
	fi
	echo "ok"
}

echo "Checking lib/rrd_api_compat.h against both RRDtool argv APIs:"
check legacy 0
check const  1
exit 0
