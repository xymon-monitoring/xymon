#!/bin/sh

# Settings for this client
MACHINEDOTS="`uname -n`"        # This systems hostname
BBOSTYPE="`uname -s | tr '[A-Z]' '[a-z]'`"	# This systems operating system in lowercase

# No modifications needed after this line
export MACHINEDOTS BBOSTYPE

BASEDIR="`dirname $0`"

case "$1" in
  "start")
	$BASEDIR/bin/hobbitlaunch --config=$BASEDIR/etc/clientlaunch.cfg --log=$BASEDIR/logs/clientlaunch.log --pidfile=$BASEDIR/logs/clientlaunch.pid
	;;

  "stop")
  	if test -f $BASEDIR/logs/clientlaunch.pid; then
		kill `cat $BASEDIR/logs/clientlaunch.pid`
	fi
	;;
esac

exit 0

