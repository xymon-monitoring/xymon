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

while test $# -ge 1; do
   case "$1" in
	"--local")  LOCALMODE="yes";;
	"--submit") SUBMITMODE="yes";;
	"--status") STATUSMODE="yes";;
	*) echo "Unknown parameter: '$1'";;
    esac
    shift
done

if test "$LOCALMODE" = "yes" -a ! -x $XYMONHOME/bin/xymond_client; then
	echo "ERROR: Local mode (--local) disabled because $XYMONHOME/bin/xymond_client missing or not executable; you may need to recompile this client or install an additional package" >&2
	LOCALMODE="no"
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
elif test "$SUBMITMODE" = "yes"; then
	echo "clientsubmit $MACHINE.$SERVEROSTYPE $CONFIGCLASS"  >>  $MSGTMPFILE
elif test "$STATUSMODE" = "yes"; then
	echo "status $MACHINE.xymonclient client $SERVEROSTYPE $CONFIGCLASS"  >>  $MSGTMPFILE
else
	echo "client $MACHINE.$SERVEROSTYPE $CONFIGCLASS"  >>  $MSGTMPFILE
fi

$XYMONHOME/bin/$XYMONOSSCRIPT >> $MSGTMPFILE
# logfiles
if test -f $LOGFETCHCFG
then
    $XYMONHOME/bin/logfetch $LOGFETCHOPTS $LOGFETCHCFG $LOGFETCHSTATUS >>$MSGTMPFILE
fi
# Client version
echo "[clientversion]"  >>$MSGTMPFILE
echo "$CLIENTVERSION"   >> $MSGTMPFILE

# See if there are any individual client sections
if test -d $XYMONHOME/sections; then
	for MODULE in `ls $XYMONHOME/sections/* 2>/dev/null | grep -v -e \.rpm -e \.dpkg`
	do
		if test -x $MODULE -a -f $MODULE
		then
			echo "[`basename $MODULE`]" >>$MSGTMPFILE
			$MODULE >>$MSGTMPFILE
		fi
	done
fi

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
	$XYMONHOME/bin/xymond_client $XYMONLOCALCLIENTOPTS --local --config=$XYMONHOME/etc/localclient.cfg <$MSGTMPFILE
elif test "$SUBMITMODE" = "yes" -o "$STATUSMODE" = "yes"; then
	if test "$XYMSRV" = "0.0.0.0" ; then
	    # Send to any/all at once -- there won't be output (except for errors)
	    for THISSRV in $XYMSERVERS; do
		$XYMON $THISSRV "@" < $MSGTMPFILE &
	    done
	    sleep 4
	else
	    $XYMON $XYMSRV "@" < $MSGTMPFILE
	fi
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

