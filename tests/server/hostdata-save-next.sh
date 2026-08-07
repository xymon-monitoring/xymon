#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
XYMOND_C="$ROOT/xymond/xymond.c"
[ -f "$XYMOND_C" ] || skip "xymond/xymond.c not present in this checkout"

source=$(cat "$XYMOND_C")
command_block=$(sed -n '/else if (strncmp(msg->buf, "hostdatasave "/,/else if (strncmp(msg->buf, "dummy"/p' "$XYMOND_C")
rename_block=$(sed -n '/case CMD_RENAMEHOST:/,/case CMD_RENAMETEST:/p' "$XYMOND_C")

assert_contains 'time_t hostdatasaveexpires;' "$source" \
	"expiry state must belong to the in-memory host record"
assert_contains '{ "hostdatasave", 0 }' "$source" \
	"hostdatasave must be included in command statistics"
assert_contains 'else if (strncmp(msg->buf, "hostdatasave ", 13) == 0)' "$command_block" \
	"hostdatasave command parser is missing"
assert_contains 'oksender(adminsenders, NULL, msg->addr.sin_addr, msg->buf)' "$command_block" \
	"administrative sender authorization is missing"
assert_contains 'if (canonhostname && clientsavemem)' "$command_block" \
	"requests must not remain pending when cached client logs are disabled"
assert_contains 'long ttlminutes = HOSTDATASAVE_DEFAULT_TTL;' "$command_block" \
	"hostdatasave must default to a 30-minute lifetime"
assert_contains '#define HOSTDATASAVE_DEFAULT_TTL 30' "$source" \
	"hostdatasave default lifetime must remain 30 minutes"
assert_contains '#define HOSTDATASAVE_MAX_TTL 1440' "$source" \
	"hostdatasave override must remain bounded to 24 hours"
assert_contains '(ttlminutes <= 0) || (ttlminutes > HOSTDATASAVE_MAX_TTL)' "$command_block" \
	"hostdatasave must reject lifetimes outside the allowed range"
assert_contains 'knownhost(hostname, hostip, GH_IGNORE)' "$command_block" \
	"hostdatasave must resolve configured hosts without accepting ghosts"
assert_contains 'hwalk = create_hostlist_t(canonhostname, hostip);' "$command_block" \
	"configured hosts must be armable before their first report"
assert_contains 'hwalk->hostdatasaveexpires = gettimer() + (ttlminutes * 60);' "$command_block" \
	"hostdatasave must store a monotonic expiry"
assert_contains 'if (hwalk && hwalk->hostdatasaveexpires)' "$source" \
	"client messages must check for an armed host"
assert_contains 'post_clientdata_to_clichg(sender, hwalk);' "$source" \
	"the next cached client message must be published to CLICHG"
assert_contains 'hwalk->hostdatasaveexpires = 0;' "$source" \
	"the request must be consumed after one client message"
assert_contains 'hwalk->hostdatasaveexpires = 0;' "$rename_block" \
	"renaming a host must cancel its pending request"

pass "hostdatasave remains an authorized, in-memory, one-shot request"
