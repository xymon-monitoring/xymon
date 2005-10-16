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
# $Id: hobbitclient.sh,v 1.6 2005-10-16 07:32:55 henrik Exp $

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

TEMPFILE="$BBTMP/msg.txt"
rm -f $TEMPFILE
touch $TEMPFILE

if test "$LOCALMODE" = "yes"; then
	echo "@@client#1|0|127.0.0.1|$MACHINEDOTS|$BBOSTYPE" >> $TEMPFILE
fi

echo "client $MACHINE.$BBOSTYPE"  >>  $TEMPFILE
$BBHOME/bin/$BBOSSCRIPT >> $TEMPFILE

if test "$LOCALMODE" = "yes"; then
	echo "@@" >> $TEMPFILE
	$BBHOME/bin/hobbitd_client --local --config=$BBHOME/etc/localclient.cfg <$TEMPFILE
else
	$BB $BBDISP "@" < $TEMPFILE
fi

exit 0

