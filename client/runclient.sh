#!/bin/sh
#----------------------------------------------------------------------------#
# Hobbit client bootup script.                                               #
#                                                                            #
# This invokes hobbitlaunch, which in turn runs the Hobbit client and any    #
# extensions configured.                                                     #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: runclient.sh,v 1.7 2006-02-13 22:01:26 henrik Exp $

# Default settings for this client
MACHINEDOTS="`uname -n`"			# This systems hostname
BBOSTYPE="`uname -s | tr '[A-Z]' '[a-z]'`"	# This systems operating system in lowercase
BBOSSCRIPT="hobbitclient-$BBOSTYPE.sh"

# Commandline mods for the defaults
while test "$1" != ""
do
	case "$1" in
	  --hostname=*)
	  	MACHINEDOTS="`echo $1 | sed -e's/--hostname=//'`"
		;;
	  --os=*)
	  	BBOSTYPE="`echo $1 | sed -e's/--os=//' | tr '[A-Z]' '[a-z]'`"
		;;
	  --class=*)
	        CONFIGCLASS="`echo $1 | sed -e's/--os=//' | tr '[A-Z]' '[a-z]'`"
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
	esac

	shift
done

OLDDIR="`pwd`"
cd "`dirname $0`"
HOBBITCLIENTHOME="`pwd`"
cd "$OLDDIR"

MACHINE="`echo $MACHINEDOTS | sed -e's/\./,/g'`"

if test "$CONFIGCLASS" = ""
then
	CONFIGCLASS="$MACHINEDOTS"
fi

export MACHINE MACHINEDOTS BBOSTYPE BBOSSCRIPT HOBBITCLIENTHOME CONFIGCLASS

case "$CMD" in
  "start")
  	if test ! -w $HOBBITCLIENTHOME/logs; then
		echo "Cannot write to the $HOBBITCLIENTHOME/logs directory"
		exit 1
	fi
  	if test ! -w $HOBBITCLIENTHOME/tmp; then
		echo "Cannot write to the $HOBBITCLIENTHOME/tmp directory"
		exit 1
	fi

  	if test -f $HOBBITCLIENTHOME/logs/clientlaunch.pid; then
		echo "Hobbit client already running, re-starting it"
		$0 stop
		rm -f $HOBBITCLIENTHOME/logs/clientlaunch.pid
	fi

	$HOBBITCLIENTHOME/bin/hobbitlaunch --config=$HOBBITCLIENTHOME/etc/clientlaunch.cfg --log=$HOBBITCLIENTHOME/logs/clientlaunch.log --pidfile=$HOBBITCLIENTHOME/logs/clientlaunch.pid
	if test $? -eq 0; then
		echo "Hobbit client for $BBOSTYPE started on $MACHINEDOTS"
	else
		echo "Hobbit client startup failed"
	fi
	;;

  "stop")
  	if test -f $HOBBITCLIENTHOME/logs/clientlaunch.pid; then
		kill `cat $HOBBITCLIENTHOME/logs/clientlaunch.pid`
		echo "Hobbit client stopped"
	else
		echo "Hobbit client not running"
	fi
	;;

  "restart")
  	if test -f $HOBBITCLIENTHOME/logs/clientlaunch.pid; then
		$0 stop
	else
		echo "Hobbit client not running, continuing to start it"
	fi

	$0 start
	;;
esac

exit 0

