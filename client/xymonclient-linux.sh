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
# rootfs, plus the historical explicit iso9660 exclusion (iso9660 is not a
# nodev type, so it is added unconditionally at run_df below). This is the
# historical upstream behavior.
if [ -r /proc/filesystems ]; then
	EXCLUDES=$(awk '$1 == "nodev" && $2 != "rootfs" { printf "%s%s", sep, $2; sep=" " }' /proc/filesystems)
else
	# Only the dynamic nodev exclusions are disabled here; the explicit iso9660
	# exclusion and the local-only df -l behavior still apply.
	echo "xymonclient-linux: /proc/filesystems not readable, dynamic nodev exclusions disabled (iso9660 exclusion and df -l still apply)" >&2
	EXCLUDES=""
fi
# Filesystem types are literal tokens, so pathname expansion must not turn a
# configured type such as "fuse.*" into filenames from the working directory.
case $- in
	*f*) FSRESTOREGLOB=no ;;
	*) FSRESTOREGLOB=yes; set -f ;;
esac
# XYMONCLIENT_FS_INCLUDE_TYPES: whitespace-separated FS types that should
# appear in the output even though they would otherwise be excluded
# (e.g. "tmpfs zfs" to surface tmpfs/zfs mounts, or "nfs nfs4 ceph" to
# include remote filesystems -- the latter also requires
# XYMONCLIENT_FS_DF_LOCAL_ONLY=no since df -l hides remote mounts).
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
# Strip leading zeros so a value like 00030 is treated as decimal 30, not as
# an over-length string (the length guard below keys on character count, not
# magnitude, so 00001 would otherwise clamp to 3600) nor as octal in the
# numeric comparison. The case above guarantees DFTIMEOUT is all digits with at
# least one non-zero, so this loop always terminates with a non-empty value.
while :; do
	case "$DFTIMEOUT" in
		0?*) DFTIMEOUT="${DFTIMEOUT#0}" ;;
		*) break ;;
	esac
done
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
	set -- "$@" -x iso9660

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
# Run df (optionally in inode mode) and reproduce the historical sed join, but
# guard the hung-filesystem case. timeout(1) reports 124 (timed out) or 137
# (128+9, our `-s KILL` case) when it has to kill df; whatever partial output it
# managed to print first is untrustworthy -- it can list the healthy mounts and
# omit the very one that hung -- so we discard it and emit an explicit failure
# marker instead of a silent (empty) section. The server reads an *empty*
# [inode] section as green ("No filesystems reporting inode data"), so a silent
# hang would otherwise be a false green; a non-empty marker that carries no
# recognisable df header drives the server's yellow "expected strings not found"
# path instead, and the same marker yellows the disk section. The same false
# green can come from any nonzero exit that prints nothing at all -- e.g.
# timeout(1) itself failing to launch df (status 125/126/127) or a BusyBox
# timeout rejecting its arguments before df runs -- so an empty section is
# emitted as a failure marker whenever the exit status was nonzero, not only for
# the kill codes. df's own nonzero exits that still print the healthy mounts
# (e.g. a single unreadable mount, exit 1) keep their partial output and flow
# through unchanged, preserving prior behaviour; an empty section with a clean
# exit 0 (the Solaris all-ZFS inode case) is also left untouched.
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
