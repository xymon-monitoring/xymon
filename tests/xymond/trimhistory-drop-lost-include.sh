#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-drop-lost-include.sh
#
# A hosts.cfg says which of its includes are allowed to be missing: "optional
# include ..." may be absent, a plain "include ..." is expected to be there.
# Both were treated the same when the file would not open -- a warning, and on
# with a host list short by everything that include carried (#512).
#
# For a renderer that is a degraded page. For "trimhistory --drop" it is data
# loss: those hosts are absent from the list, so their history files look like
# orphans, and they are deleted. Nothing in the run says the configuration was
# incomplete -- the warning names a file, not the hosts it was carrying, and the
# exit status is 0.
#
# The second half is the control, and it is why the fix reads the keyword rather
# than counting includes: "optional include" must still proceed and drop what
# really is orphaned. A fix that refused to run whenever a hosts.cfg mentioned
# an include would pass the first half and be useless.
#
# The fixture, and why HOSTSCFG carries a "!" prefix, are in
# tests/lib/trimhistory.sh.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/trimhistory.sh
. "$(dirname "$0")/../lib/trimhistory.sh"

work=$(mktempdir)
setup_trimhistory "$work"

# ---- a plain include that will not open -------------------------------------
# more.cfg is the file listing fromtheinclude.example.com. It has moved, been
# renamed, or was never there.
hosts_cfg "$work" '127.0.0.1 active.example.com # conn' "include $work/etc/more.cfg"
seed_hosthistory "$work" active.example.com
seed_hosthistory "$work" fromtheinclude.example.com

trim_rc=0
log=$(run_trimhistory "$work" --drop) || trim_rc=$?

assert_file_exists "$work/var/hist/fromtheinclude.example.com" \
	"--drop deleted the history of a host hosts.cfg does list, in an include that could not be read (#512)"
assert_file_exists "$work/var/hist/active.example.com" \
	"--drop deleted the history of a host listed in hosts.cfg itself"
[ "$trim_rc" -ne 0 ] \
	|| { echo "$log" >&2; fail "trimhistory reported success after reading a hosts.cfg it could not read in full"; }
assert_contains "include" "$log" \
	"the run refused without saying that an include was the reason"

# ---- the same include, declared optional ------------------------------------
# Now the configuration says the file may be absent, so the list is complete as
# written and fromtheinclude.example.com really is a host that no longer exists.
# It has to be dropped, exactly as it was before this fix.
rm -f "${work:?}/var/hist/"*
: >"$work/etc/hosts.cfg"
hosts_cfg "$work" '127.0.0.1 active.example.com # conn' "optional include $work/etc/more.cfg"
seed_hosthistory "$work" active.example.com
seed_hosthistory "$work" fromtheinclude.example.com

trim_rc=0
log=$(run_trimhistory "$work" --drop) || trim_rc=$?

[ "$trim_rc" -eq 0 ] \
	|| { echo "$log" >&2; fail "--drop refused on a hosts.cfg whose include is declared optional, which is the case the keyword exists for"; }
[ ! -e "$work/var/hist/fromtheinclude.example.com" ] \
	|| { echo "$log" >&2; fail "--drop kept the history of a host nothing lists: an absent optional include still leaves a complete configuration"; }
assert_file_exists "$work/var/hist/active.example.com" \
	"--drop deleted a listed host's history on a configuration that loaded completely"

# ...and the run did its ordinary work rather than stopping early.
events=$(cat "$work/var/hist/active.example.com")
assert_contains "$TRIM_RECENT" "$events" "the kept history lost its post-cutoff record"
assert_not_contains "$TRIM_ANCIENT" "$events" \
	"nothing was trimmed, so the run never got past the configuration load"

pass "--drop refuses on a hosts.cfg whose plain include could not be read, and still drops through an absent optional include"
