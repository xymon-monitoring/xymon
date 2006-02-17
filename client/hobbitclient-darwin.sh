#!/bin/sh
#
#----------------------------------------------------------------------------#
# Darwin (Mac OS X) client for Hobbit                                        #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-darwin.sh,v 1.9 2006-02-17 13:07:50 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
# The sed stuff is to make sure lines are not split into two.
df -H -T nonfs,nullfs,cd9660,procfs,volfs,devfs,fdesc | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[meminfo]"
vm_stat
echo "[ifconfig]"
ifconfig -a
echo "[route]"
netstat -r
echo "[netstat]"
netstat -s
echo "[ps]"
ps -axw
echo "[top]"
top -l 1 -n 20
# logfiles
$BBHOME/bin/logfetch $BBTMP/logfetch.cfg $BBTMP/logfetch.status

exit

