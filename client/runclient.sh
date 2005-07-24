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
# $Id: runclient.sh,v 1.2 2005-07-24 11:32:51 henrik Exp $

# Settings for this client
MACHINEDOTS="`uname -n`"			# This systems hostname
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

