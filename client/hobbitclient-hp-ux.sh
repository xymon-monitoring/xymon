#!/bin/sh
#
#----------------------------------------------------------------------------#
# HP-UX client for Hobbit                                                    #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-hp-ux.sh,v 1.6 2005-08-03 13:27:57 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
df -Pk
echo "[memory]"
$BBHOME/bin/hpux-meminfo
echo "[swapinfo]"
/usr/sbin/swapinfo -tm
echo "[netstat]"
netstat -s
echo "[ps]"
ps -ef
echo "[top]"
# Cits Bogajewski 03-08-2005: redirect of top fails
top -d 1 -f $BBHOME/tmp/top.OUT
cat $BBHOME/tmp/top.OUT
rm $BBHOME/tmp/top.OUT
echo "[vmstat]"
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi

exit

