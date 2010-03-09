#!/bin/sh

# Startup script for the Hobbit monitor
#
# This starts the "hobbitlaunch" tool, which in turn starts
# all of the other Hobbit server programs.

case "`uname -s`" in
   "SunOS")
   	ID=/usr/xpg4/bin/id
	;;
   *)
   	ID=id
	;;
esac

if test `$ID -un` != 
then
	echo "Hobbit must be started as the  user"
	exit 1
fi

case "$1" in
   "start")
	if test -s /hobbitlaunch.pid
	then
		kill -0 `cat /hobbitlaunch.pid`
		if test $? -eq 0
		then
			echo "Hobbit appears to be running, doing restart"
			$0 stop
		else
			rm -f /hobbitlaunch.pid
		fi
	fi

	 /bin/hobbitlaunch --config=/etc/hobbitlaunch.cfg --env=/etc/hobbitserver.cfg --log=/hobbitlaunch.log --pidfile=/hobbitlaunch.pid
	echo "Hobbit started"
	;;

   "stop")
	if test -s /hobbitlaunch.pid
	then
		kill -TERM `cat /hobbitlaunch.pid`
		echo "Hobbit stopped"
	else
		echo "Hobbit is not running"
	fi
	rm -f /hobbitlaunch.pid
	;;

   "status")
	if test -s /hobbitlaunch.pid
	then
		kill -0 `cat /hobbitlaunch.pid`
		if test $? -eq 0
		then
			echo "Hobbit (hobbitlaunch) running with PID `cat /hobbitlaunch.pid`"
		else
			echo "Hobbit not running, removing stale PID file"
			rm -f /hobbitlaunch.pid
		fi
	else
		echo "Hobbit (hobbitlaunch) does not appear to be running"
	fi
	;;

   "restart")
	if test -s /hobbitlaunch.pid
	then
		$0 stop
		sleep 10
		$0 start
	else
		echo "hobbitlaunch does not appear to be running, starting it"
		$0 start
	fi
	;;

   "reload")
	if test -s /hobbitd.pid
	then
		kill -HUP `cat /hobbitd.pid`
	else
		echo "hobbitd not running (no PID file)"
	fi
	;;

   "rotate")
   	for PIDFILE in /*.pid
	do
		kill -HUP `cat $PIDFILE`
	done
	;;

   *)
   	echo "Usage: $0 start|stop|restart|reload|status|rotate"
	break;
esac

exit 0

