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
# $Id: runclient.sh,v 1.3 2005-08-03 22:07:33 henrik Exp $

# Default settings for this client
MACHINEDOTS="`uname -n`"			# This systems hostname
BBOSTYPE="`uname -s | tr '[A-Z]' '[a-z]'`"	# This systems operating system in lowercase

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
	  --help)
	  	echo "Usage: $0 [--hostname=CLIENTNAME] [--os=rhel3|linux22] start|stop"
		exit 0
		;;
	  start)
	  	CMD=$1
		;;
	  stop)
	  	CMD=$1
		;;
	esac

	shift
done

BASEDIR="`dirname $0`"
MACHINE="`echo $MACHINEDOTS | sed -e's/\./,/g'`"

export MACHINE MACHINEDOTS BBOSTYPE

case "$CMD" in
  "start")
	$BASEDIR/bin/hobbitlaunch --config=$BASEDIR/etc/clientlaunch.cfg --log=$BASEDIR/logs/clientlaunch.log --pidfile=$BASEDIR/logs/clientlaunch.pid
	if test $? -eq 0; then
		echo "Hobbit client for $BBOSTYPE started on $MACHINEDOTS"
	else
		echo "Hobbit client startup failed"
	fi
	;;

  "stop")
  	if test -f $BASEDIR/logs/clientlaunch.pid; then
		kill `cat $BASEDIR/logs/clientlaunch.pid`
		echo "Hobbit client stopped"
	else
		echo "Hobbit client not running"
	fi
	;;
esac

exit 0

