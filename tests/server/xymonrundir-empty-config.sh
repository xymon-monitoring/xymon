#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonrundir-empty-config.sh
#
# A configuration line XYMONRUNDIR="" must not make the runtime directory the
# filesystem root. xgetenv() applies its default only to a missing variable,
# and tasks.cfg hands the value to the daemons through xymonlaunch's own
# expansion -- "--pidfile=$XYMONRUNDIR/xymond.pid" -- so "" became
# "/xymond.pid", created there when the server runs as root. loadenv() replaces
# an empty value with the log directory as it reads the file, so every program
# started from that configuration sees a real directory.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONCMD "common/xymoncmd"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
cfg="$work/xymonserver.cfg"
printf 'XYMONSERVERLOGS="%s/logs"\nXYMONRUNDIR=""\n' "$work" >"$cfg"

# printenv runs after the file is loaded; an argument such as $XYMONRUNDIR
# would be expanded by xymoncmd before it loads anything.
got=$(env -i PATH="$PATH" "$XYMONCMD" --env="$cfg" printenv XYMONRUNDIR 2>&1) \
	|| fail "xymoncmd could not load $cfg: $got"
[ "$got" = "$work/logs" ] \
	|| fail "XYMONRUNDIR=\"\" in the configuration left the runtime directory as [$got], not the log directory $work/logs"

# Replaced on the line that sets it, not at the end of the file: a later line
# built from it must not still see "".
printf 'XYMONSERVERLOGS="%s/logs"\nXYMONRUNDIR=""\nLATER="$XYMONRUNDIR/after"\n' "$work" >"$cfg"
got=$(env -i PATH="$PATH" "$XYMONCMD" --env="$cfg" printenv LATER 2>&1) \
	|| fail "xymoncmd could not load $cfg: $got"
[ "$got" = "$work/logs/after" ] \
	|| fail "a line after XYMONRUNDIR=\"\" expanded \$XYMONRUNDIR to [$got], not the log directory"

# And a configuration that does not set it at all: a line expanding
# $XYMONRUNDIR -- as tasks.cfg does for every pidfile -- must get the
# configured log directory, the one the programs use, not the compiled one.
printf 'XYMONSERVERLOGS="%s/logs"\nLATER="$XYMONRUNDIR/after"\n' "$work" >"$cfg"
got=$(env -i PATH="$PATH" "$XYMONCMD" --env="$cfg" printenv LATER 2>&1) \
	|| fail "xymoncmd could not load $cfg: $got"
[ "$got" = "$work/logs/after" ] \
	|| fail "with XYMONRUNDIR unset, \$XYMONRUNDIR expanded to [$got], not the configured log directory $work/logs"


# The client's own: a client configuration that does not define
# XYMONCLIENTRUNDIR -- one kept from before it existed -- must put msgcache.pid
# in the client's log directory, as clientlaunch.cfg always did, and not in a
# default built from XYMONRUNDIR, which resolves to "" in a client-only build.
printf 'XYMONCLIENTLOGS="%s/clientlogs"\nLATER="$XYMONCLIENTRUNDIR/msgcache.pid"\n' "$work" >"$cfg"
got=$(env -i PATH="$PATH" "$XYMONCMD" --env="$cfg" printenv LATER 2>&1) \
	|| fail "xymoncmd could not load $cfg: $got"
[ "$got" = "$work/clientlogs/msgcache.pid" ] \
	|| fail "with XYMONCLIENTRUNDIR undefined, the client pidfile path became [$got], not $work/clientlogs/msgcache.pid"

# and one that sets it empty: "" would put msgcache.pid at the filesystem root.
printf 'XYMONCLIENTLOGS="%s/clientlogs"\nXYMONCLIENTRUNDIR=""\nLATER="$XYMONCLIENTRUNDIR/msgcache.pid"\n' "$work" >"$cfg"
got=$(env -i PATH="$PATH" "$XYMONCMD" --env="$cfg" printenv LATER 2>&1) \
	|| fail "xymoncmd could not load $cfg: $got"
[ "$got" = "$work/clientlogs/msgcache.pid" ] \
	|| fail "with XYMONCLIENTRUNDIR=\"\", the client pidfile path became [$got], not $work/clientlogs/msgcache.pid"

pass "an empty or unset XYMONRUNDIR in the configuration resolves to the configured log directory, for the lines after it too, and an undefined or empty XYMONCLIENTRUNDIR to the client's log directory"
