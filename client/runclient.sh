#!/bin/sh
#----------------------------------------------------------------------------#
# Xymon client bootup script.                                                #
#                                                                            #
# This invokes xymonlaunch, which in turn runs the Xymon client and any      #
# extensions configured.                                                     #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
# "status" section (C) Scott Smith 2006                                      #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id$

# Default settings for this client
MACHINEDOTS="`uname -n`"			# This systems hostname
SERVEROSTYPE="`uname -s | tr '[ABCDEFGHIJKLMNOPQRSTUVWXYZ/]' '[abcdefghijklmnopqrstuvwxyz_]'`"	# This systems operating system in lowercase
XYMONOSSCRIPT="xymonclient-$SERVEROSTYPE.sh"

# Command-line mods for the defaults
while test "$1" != ""
do
	case "$1" in
	  --hostname=*)
	  	MACHINEDOTS="`echo $1 | sed -e 's/--hostname=//'`"
		;;
	  --os=*)
	  	SERVEROSTYPE="`echo $1 | sed -e 's/--os=//' | tr '[ABCDEFGHIJKLMNOPQRSTUVWXYZ/]' '[abcdefghijklmnopqrstuvwxyz_]'`"
		;;
	  --class=*)
	        CONFIGCLASS="`echo $1 | sed -e 's/--class=//' | tr '[ABCDEFGHIJKLMNOPQRSTUVWXYZ/]' '[abcdefghijklmnopqrstuvwxyz_]'`"
		;;
	  --help)
	  	echo "Usage: $0 [--hostname=CLIENTNAME] [--os=rhel3|linux22] [--class=CLASSNAME] start|stop"
		exit 0
		;;
	  start)
	  	CMD=$1
		;;
	  stop)
	  	CMD=$1
		;;
	  restart)
	  	CMD=$1
		;;
	  status)
	  	CMD=$1
		;;
	esac

	shift
done

XYMONCLIENTHOME="`dirname $0`"
if test `echo "$XYMONCLIENTHOME" | grep "^\."`
then
	# no full path, so add current directory to XYMONCLIENTHOME
	# This may give you "XYMONCLIENTHOME=/usr/local/xymon/./client" - if you 
	# run this script from /usr/local/xymon with "./client/runclient.sh" - 
	# but it works fine.
	XYMONCLIENTHOME="`pwd`/$XYMONCLIENTHOME"
fi

export MACHINEDOTS SERVEROSTYPE XYMONOSSCRIPT XYMONCLIENTHOME CONFIGCLASS

MACHINE="`echo $MACHINEDOTS | sed -e 's/\./,/g'`"
export MACHINE

case "$CMD" in
  "start")
  	if test ! -w $XYMONCLIENTHOME/logs; then
		echo "Cannot write to the $XYMONCLIENTHOME/logs directory"
		exit 1
	fi
  	if test ! -w $XYMONCLIENTHOME/tmp; then
		echo "Cannot write to the $XYMONCLIENTHOME/tmp directory"
		exit 1
	fi

  	if test -s $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid; then
		echo "Xymon client already running, re-starting it"
		$0 --hostname="$MACHINEDOTS" stop
		rm -f $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid
	fi

	$XYMONCLIENTHOME/bin/xymonlaunch --config=$XYMONCLIENTHOME/etc/clientlaunch.cfg --log=$XYMONCLIENTHOME/logs/clientlaunch.log --pidfile=$XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid
	if test $? -eq 0; then
		echo "Xymon client for $SERVEROSTYPE started on $MACHINEDOTS"
	else
		echo "Xymon client startup failed"
	fi
	;;

  "stop")
  	if test -s $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid; then
		kill `cat $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid`
		echo "Xymon client stopped"
	else
		echo "Xymon client not running"
	fi
	;;

  "restart")
  	if test -s $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid; then
		$0 --hostname="$MACHINEDOTS" stop
	else
		echo "Xymon client not running, continuing to start it"
	fi

	$0 --hostname="$MACHINEDOTS" --os="$SERVEROSTYPE" start
	;;

  "status")
	if test -s $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid
	then
		kill -0 `cat $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid`
		if test $? -eq 0
		then
			echo "Xymon client (clientlaunch) running with PID `cat $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid`"
			exit 0
		else
			echo "Xymon client not running, removing stale PID file"
			rm -f $XYMONCLIENTHOME/logs/clientlaunch.$MACHINEDOTS.pid
			exit 1
		fi
	else
		echo "Xymon client (clientlaunch) does not appear to be running"
		exit 3
	fi
	;;

  *)
	echo "Usage: $0 start|stop|restart|status"
	break;

esac

exit 0

