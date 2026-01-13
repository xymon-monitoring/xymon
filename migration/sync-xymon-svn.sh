#!/usr/bin/env bash
set -euo pipefail

SVN_URL="svn://svn.code.sf.net/p/xymon/code"
MIRROR="/tmp/xymon-svn-mirror"

echo "=== Xymon SVN mirror sync ==="
echo "SVN URL : $SVN_URL"
echo "Mirror  : $MIRROR"
echo

if [ ! -d "$MIRROR" ]; then
  svnadmin create "$MIRROR"
fi

HOOK="$MIRROR/hooks/pre-revprop-change"
if [ ! -x "$HOOK" ]; then
  printf '#!/bin/sh\nexit 0\n' > "$HOOK"
  chmod +x "$HOOK"
fi

if ! svn proplist --revprop -r 0 "file://$MIRROR" | grep -q svn:sync-from-url; then
  svnsync init --username anonymous "file://$MIRROR" "$SVN_URL"
fi

svnsync sync "file://$MIRROR"

svnlook youngest "$MIRROR"

