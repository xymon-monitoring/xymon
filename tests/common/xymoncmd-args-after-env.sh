#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/common/xymoncmd-args-after-env.sh
#
# A $VARIABLE in the command xymoncmd runs is expanded against the
# environment file, not before it is read.
#
# xymoncmd expanded its arguments while it parsed its options, before
# loadenv(): a variable the file sets was expanded to its compiled default --
# XYMONSERVERWWWNAME as XYMONHOSTNAME, XYMONRUNDIR as XYMONSERVERLOGS --
# while the command ran with the file's value in its environment. So
# `xymoncmd sh -c 'echo $XYMONWEBHOST'` gave a value no task had, and a
# configuration that was read looked ignored.
#
# Here an environment file sets three variables to values no default has:
#   - an argument naming them gets the file's values;
#   - the command's environment has the same values, as before;
#   - a variable the file does not set still gets its default in an argument;
#   - with no command, xymoncmd still runs /bin/sh.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

default="common/xymoncmd"
if [ -z "${XYMONCMD:-}" ] && [ ! -x "$(find_root)/$default" ] \
		&& [ -x "$(find_root)/client/xymoncmd" ]; then
	default="client/xymoncmd"
fi
require_bin XYMONCMD "$default"

work=$(mktempdir)
cat >"$work/xymonserver.cfg" <<'EOF'
XYMONSERVERLOGS="/from/file/logs"
XYMONRUNDIR="/from/file/run"
XYMONSERVERWWWNAME="from-file.example"
XYMONWEBHOST="http://$XYMONSERVERWWWNAME:8080"
EOF

# cmd ARGS... -- xymoncmd with the file above, in an environment that sets
# none of these variables, so that only the file or a default can.
cmd() { env -i HOME="$work" PATH=/usr/bin:/bin "$XYMONCMD" --env="$work/xymonserver.cfg" "$@" 2>&1; }

out=$(cmd /bin/echo 'W=$XYMONSERVERWWWNAME H=$XYMONWEBHOST R=$XYMONRUNDIR')
assert_equal "W=from-file.example H=http://from-file.example:8080 R=/from/file/run" "$out" \
	"an argument's \$VARIABLE was expanded before the file was read"

assert_equal "from-file.example" "$(cmd printenv XYMONSERVERWWWNAME)" \
	"the command's environment lost the file's value"
assert_equal "/from/file/run" "$(cmd printenv XYMONRUNDIR)" \
	"the command's environment lost the file's XYMONRUNDIR"

assert_equal "U=/xymon" "$(cmd /bin/echo 'U=$XYMONSERVERWWWURL')" \
	"a variable the file does not set lost its default in an argument"

assert_equal "ok" "$(echo 'echo ok' | cmd)" \
	"with no command, xymoncmd did not run /bin/sh"

pass "xymoncmd expands its command's \$VARIABLEs against the environment file, as its command's environment has them"
