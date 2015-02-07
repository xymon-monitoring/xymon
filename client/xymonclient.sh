#!/bin/sh
#----------------------------------------------------------------------------#
# Xymon client main script.                                                  #
#                                                                            #
# This invokes the OS-specific script to build a client message, and sends   #
# if off to the Xymon server.                                                #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id$

# Must make sure the commands return standard (english) texts.
LANG=C
LC_ALL=C
LC_MESSAGES=C
export LANG LC_ALL LC_MESSAGES

LOCALMODE="no"
if test $# -ge 1; then
	if test "$1" = "--local"; then
		LOCALMODE="yes"
	fi
	shift
fi

if test "$XYMONOSSCRIPT" = ""; then
	XYMONOSSCRIPT="xymonclient-`uname -s | tr '[ABCDEFGHIJKLMNOPQRSTUVWXYZ/]' '[abcdefghijklmnopqrstuvwxyz_]'`.sh"
fi

MSGFILE="$XYMONTMP/msg.$MACHINEDOTS.txt"
MSGTMPFILE="$MSGFILE.$$"
LOGFETCHCFG=$XYMONTMP/logfetch.$MACHINEDOTS.cfg
LOGFETCHSTATUS=$XYMONTMP/logfetch.$MACHINEDOTS.status
export LOGFETCHCFG LOGFETCHSTATUS

rm -f $MSGTMPFILE
touch $MSGTMPFILE


CLIENTVERSION="`$XYMONHOME/bin/clientupdate --level`"
if test -z "$CLIENTVERSION"; then
	CLIENTVERSION="`$XYMON --version`"
fi

if test "$LOCALMODE" = "yes"; then
	echo "@@client#1|1|127.0.0.1|$MACHINEDOTS|$SERVEROSTYPE" >> $MSGTMPFILE
fi

echo "client $MACHINE.$SERVEROSTYPE $CONFIGCLASS"  >>  $MSGTMPFILE
$XYMONHOME/bin/$XYMONOSSCRIPT >> $MSGTMPFILE
# logfiles
if test -f $LOGFETCHCFG
then
    $XYMONHOME/bin/logfetch $LOGFETCHCFG $LOGFETCHSTATUS >>$MSGTMPFILE
fi
# Client version
echo "[clientversion]"  >>$MSGTMPFILE
echo "$CLIENTVERSION"   >> $MSGTMPFILE

# See if there are any local add-ons (must do this before checking the clock)
if test -d $XYMONHOME/local; then
	for MODULE in $XYMONHOME/local/*
	do
		if test -x $MODULE
		then
			echo "[local:`basename $MODULE`]" >>$MSGTMPFILE
			$MODULE >>$MSGTMPFILE
		fi
	done
fi

# System clock
echo "[clock]"          >> $MSGTMPFILE
$XYMONHOME/bin/logfetch --clock >> $MSGTMPFILE

if test "$LOCALMODE" = "yes"; then
	echo "@@" >> $MSGTMPFILE
	$XYMONHOME/bin/xymond_client --local --config=$XYMONHOME/etc/localclient.cfg <$MSGTMPFILE
else
	$XYMON $XYMSRV "@" < $MSGTMPFILE >$LOGFETCHCFG.tmp
	if test -f $LOGFETCHCFG.tmp
	then
		if test -s $LOGFETCHCFG.tmp
		then
			mv $LOGFETCHCFG.tmp $LOGFETCHCFG
		else
			rm -f $LOGFETCHCFG.tmp
		fi
	fi
fi

# Save the latest file for debugging.
rm -f $MSGFILE
mv $MSGTMPFILE $MSGFILE

if test "$LOCALMODE" != "yes" -a -f $LOGFETCHCFG; then
	# Check for client updates
	SERVERVERSION=`grep "^clientversion:" $LOGFETCHCFG | cut -d: -f2`
	if test "$SERVERVERSION" != "" -a "$SERVERVERSION" != "$CLIENTVERSION"; then
		exec $XYMONHOME/bin/clientupdate --update=$SERVERVERSION --reexec
	fi
fi

exit 0

