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
# Default: exclude every nodev (pseudo) filesystem in df/inode output, except
# rootfs. This is the historical upstream behavior.
if [ -r /proc/filesystems ]; then
	EXCLUDES=`awk '/nodev/ {print $2}' /proc/filesystems | grep -v '^rootfs$' | xargs echo`
else
	echo "xymonclient-linux: /proc/filesystems not readable, filesystem filter disabled" >&2
	EXCLUDES=""
fi
# XYMONCLIENT_FS_INCLUDE_TYPES: whitespace-separated FS types that should
# appear in the output even though they would otherwise be excluded
# (e.g. "tmpfs squashfs" to surface tmpfs mounts, or "nfs nfs4 ceph" to
# include remote filesystems -- the latter also requires
# XYMONCLIENT_FS_DF_LOCAL_ONLY=no since df -l hides remote mounts).
if [ -n "$XYMONCLIENT_FS_INCLUDE_TYPES" ]; then
	for t in $XYMONCLIENT_FS_INCLUDE_TYPES; do
		EXCLUDES=`echo " $EXCLUDES " | sed -e "s! $t ! !g"`
	done
fi
# XYMONCLIENT_FS_EXCLUDE_TYPES: whitespace-separated FS types to ALSO
# exclude, on top of the nodev default (e.g. "overlay fuse" to drop
# container-runtime mounts that would otherwise leak into the report).
if [ -n "$XYMONCLIENT_FS_EXCLUDE_TYPES" ]; then
	for t in $XYMONCLIENT_FS_EXCLUDE_TYPES; do
		case " $EXCLUDES " in
			*" $t "*) ;;  # already in list
			*) EXCLUDES="$EXCLUDES $t" ;;
		esac
	done
fi
EXCLUDES=`echo $EXCLUDES | sed -e 's! ! -x !g'`
# XYMONCLIENT_FS_DF_LOCAL_ONLY: defaults to "yes" (current upstream behavior,
# passes -l to df). Set to "no" to drop -l so that remote filesystems
# (nfs, ceph, ...) appear in the output.
DFOPTS="-P"
[ "${XYMONCLIENT_FS_DF_LOCAL_ONLY:-yes}" = "yes" ] && DFOPTS="$DFOPTS -l"
# XYMONCLIENT_FS_DF_TIMEOUT: seconds before df is killed (default 30).
# Set to empty string ("") to disable. df can hang on stale NFS/CIFS mounts
# -- particularly relevant when XYMONCLIENT_FS_DF_LOCAL_ONLY=no.
DFTIMEOUT="${XYMONCLIENT_FS_DF_TIMEOUT-30}"
if [ -n "$DFTIMEOUT" ] && command -v timeout >/dev/null 2>&1; then
	DFCMD="timeout ${DFTIMEOUT}s df"
else
	DFCMD="df"
fi
ROOTFS=`readlink -m /dev/root`
$DFCMD $DFOPTS -x iso9660 ${EXCLUDES:+-x $EXCLUDES} | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}' -e "s&^rootfs&${ROOTFS}&"
echo "[inode]"
$DFCMD $DFOPTS -i -x iso9660 ${EXCLUDES:+-x $EXCLUDES} | sed -e '/^[^ 	][^ 	]*$/{
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
# For some reason, the option for Wide/unTrimmed display of IPs
# changed in netstat versions and no one provided backwards compat,
# so exactly ONE of these should work successfully:
netstat -antuW 2>/dev/null
netstat -antuT 2>/dev/null
echo "[ifstat]"
/sbin/ifconfig 2>/dev/null
# Report mdstat data if it exists
if test -r /proc/mdstat; then echo "[mdstat]"; cat /proc/mdstat; fi
echo "[ps]"
ps -Aww f -o pid,ppid,user,start,state,pri,pcpu,time:12,pmem,rsz:10,vsz:10,cmd
if command -v dpkg >/dev/null 2>&1; then
	echo "[dpkg]"
	COLUMNS=200 dpkg -l | awk '/^..  / { print $1 " " $2 " " $3 }'
fi

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
	echo "[nproc]"
	nproc --all 2>/dev/null
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

