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
# rootfs. The always-100%-full read-only images (iso9660, squashfs) are not
# nodev types and are excluded via XYMONCLIENT_FS_EXCLUDE_TYPES below.
if [ -r /proc/filesystems ]; then
	EXCLUDES=$(awk '$1 == "nodev" && $2 != "rootfs" { printf "%s%s", sep, $2; sep=" " }' /proc/filesystems)
else
	# Only the dynamic nodev exclusions are disabled here; the EXCLUDE_TYPES
	# defaults (iso9660/squashfs) and the local-only df -l behavior still apply.
	echo "xymonclient-linux: /proc/filesystems not readable, dynamic nodev exclusions disabled (EXCLUDE_TYPES and df -l still apply)" >&2
	EXCLUDES=""
fi
# Filesystem types are literal tokens, so pathname expansion must not turn a
# configured type such as "fuse.*" into filenames from the working directory.
case $- in
	*f*) FSRESTOREGLOB=no ;;
	*) FSRESTOREGLOB=yes; set -f ;;
esac
# XYMONCLIENT_FS_INCLUDE_TYPES: whitespace-separated FS types to surface even
# though they are flagged nodev and would otherwise be dropped. Defaults to the
# real local filesystems that merely happen to be nodev: zfs (pools have no
# single block device), virtiofs (VM-shared storage) and tmpfs (RAM-backed --
# the noisy /run* tmpfs mounts are filtered server-side in analysis.cfg). Set
# to "" to restore the historical "exclude every nodev type" behaviour, or add
# remote types e.g. "nfs nfs4 ceph" (which also needs
# XYMONCLIENT_FS_DF_LOCAL_ONLY=no, since df -l hides remote mounts).
: "${XYMONCLIENT_FS_INCLUDE_TYPES=zfs virtiofs tmpfs}"
if [ -n "$XYMONCLIENT_FS_INCLUDE_TYPES" ]; then
	# Exact token comparison -- a type is matched literally, never as a
	# regex/glob, so e.g. "fuse.sshfs" cannot collide with another token.
	keep=""
	for e in $EXCLUDES; do
		drop=no
		for t in $XYMONCLIENT_FS_INCLUDE_TYPES; do
			[ "$e" = "$t" ] && { drop=yes; break; }
		done
		[ "$drop" = no ] && keep="$keep $e"
	done
	EXCLUDES="$keep"
fi
# XYMONCLIENT_FS_EXCLUDE_TYPES: whitespace-separated FS types to ALSO exclude,
# on top of the nodev default. Defaults to "iso9660 squashfs" -- both are
# read-only images reported 100% full by design (snap mounts one squashfs per
# revision), and neither is a nodev type, so they must be named here. Matching
# is on the exact df type token; nodev types (overlay, fuse, ...) are already
# excluded, so other effective entries name a non-nodev type, e.g. adding
# "fuse.sshfs vfat" to drop a specific FUSE subtype and a device-backed mount.
# Set to "" to monitor iso9660/squashfs too.
: "${XYMONCLIENT_FS_EXCLUDE_TYPES=iso9660 squashfs}"
if [ -n "$XYMONCLIENT_FS_EXCLUDE_TYPES" ]; then
	for t in $XYMONCLIENT_FS_EXCLUDE_TYPES; do
		case " $EXCLUDES " in
			*" $t "*) ;;  # already in list
			*) EXCLUDES="$EXCLUDES $t" ;;
		esac
	done
fi
[ "$FSRESTOREGLOB" = yes ] && set +f
# XYMONCLIENT_FS_DF_LOCAL_ONLY: defaults to "yes" (current upstream behavior,
# passes -l to df). Set to "no" to drop -l so that remote filesystems
# (nfs, ceph, ...) appear in the output.
DFLOCALONLY="${XYMONCLIENT_FS_DF_LOCAL_ONLY:-yes}"
case "$DFLOCALONLY" in
	yes|no) ;;
	*)
		echo "xymonclient-linux: invalid XYMONCLIENT_FS_DF_LOCAL_ONLY '$DFLOCALONLY', using yes" >&2
		DFLOCALONLY=yes
		;;
esac
# XYMONCLIENT_FS_DF_TIMEOUT: seconds before timeout(1) sends SIGKILL to df
# (default 30). df can hang on stale NFS/CIFS mounts -- particularly relevant
# when XYMONCLIENT_FS_DF_LOCAL_ONLY=no. Empty or unset falls back to the
# default; the timeout cannot be disabled through this setting, and its value
# can be raised only up to the fixed 3600s cap below.
DFTIMEOUT="${XYMONCLIENT_FS_DF_TIMEOUT:-30}"
# A non-numeric value or zero is rejected (zero means "no timeout" under GNU
# coreutils but "fire immediately" under BusyBox, so it is never safe) and
# falls back to the 30s default with a warning.
case "$DFTIMEOUT" in
	*[!0-9]*)
		echo "xymonclient-linux: invalid XYMONCLIENT_FS_DF_TIMEOUT '$DFTIMEOUT', using 30" >&2
		DFTIMEOUT=30
		;;
	*[1-9]*) ;;
	*)
		echo "xymonclient-linux: invalid XYMONCLIENT_FS_DF_TIMEOUT '$DFTIMEOUT', using 30" >&2
		DFTIMEOUT=30
		;;
esac
# Normalise to decimal (expr is decimal by spec) so a zero-padded value like
# 00030 becomes 30: the length guard below keys on character count, and the
# numeric comparison would otherwise read a leading zero as octal.
DFTIMEOUT=`expr "$DFTIMEOUT" + 0`
# Cap at 3600s. BusyBox timeout(1) rejects an out-of-range duration with a
# nonzero exit before df runs, which would silently empty both the df and
# inode sections; an hour is already far past any sane df wait. The length
# guard short-circuits the numeric comparison so it never overflows the
# shell's integer type on an absurdly long digit string.
if [ "${#DFTIMEOUT}" -gt 4 ] || [ "$DFTIMEOUT" -gt 3600 ]; then
	echo "xymonclient-linux: XYMONCLIENT_FS_DF_TIMEOUT '$DFTIMEOUT' exceeds 3600, using 3600" >&2
	DFTIMEOUT=3600
fi
run_df()
{
	DFINODES="$1"
	set -- -P
	[ "$DFLOCALONLY" = yes ] && set -- "$@" -l
	[ "$DFINODES" = yes ] && set -- "$@" -i

	case $- in
		*f*) DFRESTOREGLOB=no ;;
		*) DFRESTOREGLOB=yes; set -f ;;
	esac
	for t in $EXCLUDES; do
		set -- "$@" -x "$t"
	done
	[ "$DFRESTOREGLOB" = yes ] && set +f

	if command -v timeout >/dev/null 2>&1; then
		timeout -s KILL "${DFTIMEOUT}s" df "$@"
	else
		df "$@"
	fi
}
# emit_df INODEFLAG LABEL
# Run df (optionally in inode mode) and reproduce the historical sed join.
# The server reads an empty section as green, so a hung/failed df must not pass
# silently: on a timeout kill (124/137) or any nonzero exit with no output, emit
# a failure marker (no recognisable df header) to drive the server yellow. A
# nonzero exit that still prints mounts (e.g. one unreadable mount) and a clean
# empty exit 0 (Solaris all-ZFS inodes) keep their output unchanged.
emit_df()
{
	DFOUT=`run_df "$1"`
	DFRC=$?
	case $DFRC in
		124|137)
			echo "xymonclient-linux: df $2 collection timed out after ${DFTIMEOUT}s (timeout status $DFRC); reporting data as unavailable" >&2
			echo "$2 collection failed: df timed out after ${DFTIMEOUT}s (status $DFRC)"
			return
			;;
	esac
	if [ -z "$DFOUT" ]; then
		[ "$DFRC" -eq 0 ] && return
		echo "xymonclient-linux: df $2 collection failed (status $DFRC) with no output; reporting data as unavailable" >&2
		echo "$2 collection failed: df exited $DFRC with no output"
		return
	fi
	printf '%s\n' "$DFOUT" | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}' -e "s&^rootfs&${ROOTFS}&"
}
ROOTFS=`readlink -m /dev/root`
emit_df no Disk
echo "[inode]"
emit_df yes Inode
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
