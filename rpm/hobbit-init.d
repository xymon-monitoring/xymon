#! /bin/sh
#
# hobbit          This shell script takes care of starting and stopping
#                 hobbit(the Xymon network monitor)
#
# chkconfig: 2345 80 20
# description: hobbit is a network monitoring tool that allows \
# you to monitor hosts and services. The monitor status is available \
# via a webpage.

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/usr/lib/hobbit/server/hobbit.sh
NAME=hobbit
DESC=hobbit

test -x $DAEMON || exit 0

# Include hobbit defaults if available
if [ -f /etc/default/hobbit ] ; then
	. /etc/default/hobbit
fi

set -e

case "$1" in
  start)
	echo -n "Starting $DESC: "
	su -c "$DAEMON start" - hobbit
	echo "$NAME."
	;;
  stop)
	echo -n "Stopping $DESC: "
	su -c "$DAEMON stop" - hobbit
	echo "$NAME."
	;;
  reload|force-reload)
	echo "Reloading $DESC configuration files."
	su -c "$DAEMON reload" - hobbit
	echo "$NAME."
	  ;;
  restart)
	echo -n "Restarting $DESC: "
	su -c "$DAEMON restart" - hobbit
	echo "$NAME."
	;;
  rotate)
	echo -n "Rotating logs for $DESC: "
	su -c "$DAEMON rotate" - hobbit
	echo "$NAME."
	;;
  *)
	N=/etc/init.d/$NAME
	# echo "Usage: $N {start|stop|restart|reload|force-reload}" >&2
	echo "Usage: $N {start|stop|restart|force-reload}" >&2
	exit 1
	;;
esac

exit 0

