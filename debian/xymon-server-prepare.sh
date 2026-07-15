#!/bin/sh

. /usr/share/xymon/init-common.sh

if [ -f /etc/default/xymon-client ] ; then
	. /etc/default/xymon-client
fi

create_includefiles
