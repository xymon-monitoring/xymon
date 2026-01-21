#! /bin/sh
#
# xymon           This shell script takes care of starting and stopping
#                 xymon (the Xymon network monitor)
#
# chkconfig: 2345 80 20
# description: Xymon  is a network monitoring tool that allows \
# you to monitor hosts and services. The monitor status is available \
# via a webpage.

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/usr/lib/xymon/server/bin/xymon.sh
NAME=xymon
DESC=Xymon

test -x $DAEMON || exit 0

# Include Xymon defaults if available
if [ -f /etc/default/xymon ] ; then
	. /etc/default/xymon
fi

set -e

case "$1" in
  start)
	echo -n "Starting $DESC: "
	su -c "$DAEMON start" - xymon
	echo "$NAME."
	;;
  stop)
	echo -n "Stopping $DESC: "
	su -c "$DAEMON stop" - xymon
	echo "$NAME."
	;;
  status)
	su -c "$DAEMON status" - xymon
	;;
  reload|force-reload)
	echo "Reloading $DESC configuration files."
	su -c "$DAEMON reload" - xymon
	echo "$NAME."
	  ;;
  restart)
	echo -n "Restarting $DESC: "
	su -c "$DAEMON restart" - xymon
	echo "$NAME."
	;;
  rotate)
	echo -n "Rotating logs for $DESC: "
	su -c "$DAEMON rotate" - xymon
	echo "$NAME."
	;;
  *)
	N=/etc/init.d/$NAME
	# echo "Usage: $N {start|stop|restart|status|reload|force-reload}" >&2
	echo "Usage: $N {start|stop|restart|status|force-reload}" >&2
	exit 1
	;;
esac

exit 0

