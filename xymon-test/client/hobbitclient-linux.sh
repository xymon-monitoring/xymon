#!/bin/sh
#----------------------------------------------------------------------------#
# Linux client for Hobbit                                                    #
#                                                                            #
# Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-linux.sh 6125 2009-02-12 13:09:34Z storner $

echo "[date]"
date
echo "[uname]"
uname -rsmn
echo "[osversion]"
if [ -x /bin/lsb_release ]; then
	/bin/lsb_release -r -i -s | xargs echo
	/bin/lsb_release -a
elif [ -f /etc/redhat-release ]; then
	cat /etc/redhat-release
elif [ -f /etc/gentoo-release ]; then
	cat /etc/gentoo-release
elif [ -f /etc/debian_version ]; then
	echo -en "Debian "
	cat /etc/debian_version
elif [ -f /etc/S?SE-release ]; then
	egrep -i "^suse|^opensuse" /etc/S?SE-release
elif [ -f /etc/slackware-version ]; then
	cat /etc/slackware-version
elif [ -f /etc/mandrake-release ]; then
	cat /etc/mandrake-release
elif [ -f /etc/fedora-release ]; then
	cat /etc/fedora-release
elif [ -f /etc/arch-release ]; then
	cat /etc/arch-release
fi
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
EXCLUDES=`cat /proc/filesystems | grep nodev | awk '{print $2}' | xargs echo | sed -e 's! ! -x !g'`
df -Pl -x iso9660 -x $EXCLUDES | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[mount]"
mount
echo "[free]"
free
echo "[ifconfig]"
/sbin/ifconfig
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ports]"
# Bug in RedHat's netstat spews annoying error messages. 
netstat -ant 2>/dev/null
echo "[ifstat]"
/sbin/ifconfig
# Report mdstat data if it exists
if test -r /proc/mdstat; then echo "[mdstat]"; cat /proc/mdstat; fi
echo "[ps]"
ps -Aww -o pid,ppid,user,start,state,pri,pcpu,time,pmem,rsz,vsz,cmd

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
	$TOP -b -n 1 
    fi
fi

# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ $BBTMP/hobbit_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat.$MACHINEDOTS; rm -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; fi

exit

