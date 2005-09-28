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
# $Id: hobbitclient.sh,v 1.5 2005-09-28 21:21:17 henrik Exp $

# Must make sure the commands return standard (english) texts.
LANG=C
LC_ALL=C
LC_MESSAGES=C
export LANG LC_ALL LC_MESSAGES

if test "$BBOSSCRIPT" = ""; then
	BBOSSCRIPT="hobbitclient-`uname -s | tr '[A-Z]' '[a-z]'`.sh"
fi

echo "client $MACHINE.$BBOSTYPE"      >  $BBTMP/msg.txt
$BBHOME/bin/$BBOSSCRIPT >> $BBTMP/msg.txt
$BB $BBDISP "@" < $BBTMP/msg.txt

exit 0

