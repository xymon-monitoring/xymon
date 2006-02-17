#!/bin/sh
#----------------------------------------------------------------------------#
# Hobbit client main script.                                                 #
#                                                                            #
# This invokes the OS-specific script to build a client message, and sends   #
# if off to the Hobbit server.                                               #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient.sh,v 1.10 2006-02-17 13:07:50 henrik Exp $

# Must make sure the commands return standard (english) texts.
LANG=C
LC_ALL=C
LC_MESSAGES=C
export LANG LC_ALL LC_MESSAGES

if test "$CONFIGCLASS" = ""
then
	CONFIGCLASS="$BBOSTYPE"
	export CONFIGCLASS
fi

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

TEMPFILE="$BBTMP/msg.txt.$$"
rm -f $TEMPFILE
touch $TEMPFILE

if test "$LOCALMODE" = "yes"; then
	echo "@@client#1|0|127.0.0.1|$MACHINEDOTS|$BBOSTYPE" >> $TEMPFILE
fi

echo "client $MACHINE.$BBOSTYPE $CONFIGCLASS"  >>  $TEMPFILE
$BBHOME/bin/$BBOSSCRIPT >> $TEMPFILE

if test "$LOCALMODE" = "yes"; then
	echo "@@" >> $TEMPFILE
	$BBHOME/bin/hobbitd_client --local --config=$BBHOME/etc/localclient.cfg <$TEMPFILE
else
	$BB $BBDISP "@" < $TEMPFILE >$BBTMP/logfetch.cfg.tmp
	if test -s $BBTMP/logfetch.cfg.tmp
	then
		mv $BBTMP/logfetch.cfg.tmp $BBTMP/logfetch.cfg
	else
		rm $BBTMP/logfetch.cfg.tmp
	fi
fi

# Save the latest file for debugging.
rm -f $BBTMP/msg.txt
mv $TEMPFILE $BBTMP/msg.txt

exit 0

