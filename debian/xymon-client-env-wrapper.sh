#!/bin/sh

XYMONCLIENTHOME="/usr/lib/xymon/client"

if [ -f /etc/default/xymon-client ] ; then
	. /etc/default/xymon-client
fi

[ -z "$MACHINE" ] && MACHINE="$CLIENTHOSTNAME"
[ -z "$MACHINEDOTS" ] && MACHINEDOTS="$(hostname -f)"
export XYMONSERVERS XYMONCLIENTHOME CLIENTHOSTNAME MACHINE MACHINEDOTS

exec "$@"
