#!/bin/sh
#----------------------------------------------------------------------------#
# Linux client for Xymon                                                     #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id$

echo "[date]"
date
echo "[uname]"
uname -rsmn
echo "[osversion]"
if [ -x /bin/lsb_release ]; then
	/bin/lsb_release -r -i -s | xargs echo
	/bin/lsb_release -a 2>/dev/null
elif [ -x /usr/bin/lsb_release ]; then
	/usr/bin/lsb_release -r -i -s | xargs echo
	/usr/bin/lsb_release -a 2>/dev/null
elif [ -f /etc/redhat-release ]; then
	cat /etc/redhat-release
elif [ -f /etc/gentoo-release ]; then
	cat /etc/gentoo-release
elif [ -f /etc/debian_version ]; then
	echo -n "Debian "
	cat /etc/debian_version
elif [ -f /etc/S?SE-release ]; then
	cat /etc/S?SE-release
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
EXCLUDES=`cat /proc/filesystems | grep nodev | grep -v rootfs | awk '{print $2}' | xargs echo | sed -e 's! ! -x !g'`
ROOTFS=`readlink -m /dev/root`
df -Pl -x iso9660 -x $EXCLUDES | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}' -e "s&^rootfs&${ROOTFS}&"
echo "[inode]"
df -Pil -x iso9660 -x $EXCLUDES | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}' -e "s&^rootfs&${ROOTFS}&"
echo "[mount]"
mount
echo "[free]"
free
echo "[ifconfig]"
/sbin/ifconfig 2>/dev/null
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ports]"
# Bug in RedHat's netstat spews annoying error messages. 
netstat -antu 2>/dev/null
echo "[ifstat]"
/sbin/ifconfig 2>/dev/null
# Report mdstat data if it exists
if test -r /proc/mdstat; then echo "[mdstat]"; cat /proc/mdstat; fi
echo "[ps]"
ps -Aww f -o pid,ppid,user,start,state,pri,pcpu,time:12,pmem,rsz:10,vsz:10,cmd

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
	export CPULOOP ; CPULOOP=1 ;
	$TOP -b -n 1 
	# Some top versions do not finish off the last line of output
	echo ""
    fi
fi

# vmstat
nohup sh -c "vmstat 300 2 1>$XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ $XYMONTMP/xymon_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $XYMONTMP/xymon_vmstat.$MACHINEDOTS; rm -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; fi

exit

