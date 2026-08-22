#!/bin/sh

. /usr/share/xymon/init-common.sh

if [ -f /etc/default/xymon-client ] ; then
	. /etc/default/xymon-client
fi

create_includefiles

if test "$TMPFSSIZE" && test -e /proc/mounts && ! grep -q /var/lib/xymon/tmp /proc/mounts; then
	echo "Mounting tmpfs on /var/lib/xymon/tmp"
	rm -f /var/lib/xymon/tmp/*
	mount -t tmpfs -o"size=$TMPFSSIZE,mode=755,uid=$(id -u xymon)" tmpfs /var/lib/xymon/tmp
fi
