#!/bin/sh
#----------------------------------------------------------------------------#
# Hobbit client main script.                                                 #
#                                                                            #
# This invokes the OS-specific script to build a client message, and sends   #
# if off to the Hobbit server.                                               #
#                                                                            #
# Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient.sh,v 1.12 2006-05-14 20:09:47 henrik Exp $

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

if test "$BBOSSCRIPT" = ""; then
	BBOSSCRIPT="hobbitclient-`uname -s | tr '[A-Z]' '[a-z]'`.sh"
fi

MSGFILE="$BBTMP/msg.$MACHINEDOTS.txt"
MSGTMPFILE="$MSGFILE.$$"
LOGFETCHCFG=$BBTMP/logfetch.$MACHINEDOTS.cfg
LOGFETCHSTATUS=$BBTMP/logfetch.$MACHINEDOTS.status
export LOGFETCHCFG LOGFETCHSTATUS

rm -f $MSGTMPFILE
touch $MSGTMPFILE


CLIENTVERSION="`$BBHOME/bin/clientupdate --level`"

if test "$LOCALMODE" = "yes"; then
	echo "@@client#1|0|127.0.0.1|$MACHINEDOTS|$BBOSTYPE" >> $MSGTMPFILE
fi

echo "client $MACHINE.$BBOSTYPE $CONFIGCLASS"  >>  $MSGTMPFILE
$BBHOME/bin/$BBOSSCRIPT >> $MSGTMPFILE
echo "[clientversion]" >>$MSGTMPFILE
echo "$CLIENTVERSION" >> $MSGTMPFILE

if test "$LOCALMODE" = "yes"; then
	echo "@@" >> $MSGTMPFILE
	$BBHOME/bin/hobbitd_client --local --config=$BBHOME/etc/localclient.cfg <$MSGTMPFILE
else
	$BB $BBDISP "@" < $MSGTMPFILE >$LOGFETCHCFG.tmp
	if test -s $LOGFETCHCFG.tmp
	then
		mv $LOGFETCHCFG.tmp $LOGFETCHCFG
	else
		rm $LOGFETCHCFG.tmp
	fi
fi

# Save the latest file for debugging.
rm -f $MSGFILE
mv $MSGTMPFILE $MSGFILE

if test "$LOCALMODE" != "yes"; then
	# Check for client updates
	SERVERVERSION=`grep "^clientversion:" $LOGFETCHCFG | cut -d: -f2`
	if test "$SERVERVERSION" != "" -a "$SERVERVERSION" != "$CLIENTVERSION"; then
		cp -pf $BBHOME/bin/clientupdate $BBTMP/.update.$MACHINE
		exec $BBTMP/.update.$MACHINE --update=$SERVERVERSION
	fi
fi

exit 0

