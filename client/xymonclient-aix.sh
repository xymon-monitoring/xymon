#!/bin/sh
#----------------------------------------------------------------------------#
# AIX client for Xymon                                                       #
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
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
# --- filesystem filter (configurable; see xymonclient.cfg.DIST) --------------
# Requires AIX 7.1: "df -T local" below does not exist before it (RELEASENOTES).
# AIX df cannot exclude by type, so excluded types are dropped from its output
# by mount point, via the vfs column of mount(8). Default set, minus
# INCLUDE_TYPES, plus EXCLUDE_TYPES, like the other clients.
: "${XYMONCLIENT_FS_INCLUDE_TYPES=}"
: "${XYMONCLIENT_FS_EXCLUDE_TYPES=}"
# noglob: a configured type like "procf*" stays literal.
case $- in *f*) _restoreglob=no ;; *) _restoreglob=yes; set -f ;; esac
FSEXCL=""
for _t in procfs ahafs namefs autofs cdrfs; do
	for _i in $XYMONCLIENT_FS_INCLUDE_TYPES; do [ "$_t" = "$_i" ] && continue 2; done
	FSEXCL="$FSEXCL $_t"
done
[ "$_restoreglob" = yes ] && set +f
# EXCLUDE_TYPES last: a type in both lists stays excluded.
FSEXCL="$FSEXCL $XYMONCLIENT_FS_EXCLUDE_TYPES"
# The excluded mounts, as "device mountpoint" pairs: an excluded overlay type
# (namefs) can share its mount point with a real filesystem, and only the
# overlay row may go. mount prints two header lines; a local row carries the
# vfs type in column 3, a remote row (leading node column, so column 3 is the
# slash-starting mount point) in column 4, its device rendering as node:path
# in df. The lists reach awk via ENVIRON: -v would apply awk escape processing.
EXCLMP=$(mount | FSEXCL="$FSEXCL" awk '
	BEGIN { n = split(ENVIRON["FSEXCL"], t, " "); for (i = 1; i <= n; i++) T[t[i]] = 1 }
	NR <= 2 { next }
	{ if ($3 ~ /^\//) { vfs = $4; dev = $1 ":" $2; mp = $3 } else { vfs = $3; dev = $1; mp = $2 } }
	vfs in T { print dev " " mp }
')
# XYMONCLIENT_FS_DF_LOCAL_ONLY: defaults to "yes", like the other clients. AIX
# df has no -l, but "-T local" is the same selection and the same cure: a df
# that never touches a remote mount cannot wedge on one. A hard-mounted NFS
# server that stops answering otherwise hangs df here for as long as it takes
# the host to reboot, and the whole client run with it. Set to "no" to report
# remote filesystems too, accepting that risk - AIX has no remote-df sentinel
# to bound it, unlike the Linux, BSD and macOS clients.
DFLOCALONLY="${XYMONCLIENT_FS_DF_LOCAL_ONLY:-yes}"
case "$DFLOCALONLY" in
	yes|no) ;;
	*)
		echo "xymonclient-aix: invalid XYMONCLIENT_FS_DF_LOCAL_ONLY '$DFLOCALONLY', using yes" >&2
		DFLOCALONLY=yes
		;;
esac

# Spelled out per branch, not expanded from a variable: an unquoted expansion
# splits on IFS, and a caller with a non-default IFS would hand df "-T local"
# as one argument.
# The sed stuff is to make sure lines are not split into two.
if [ "$DFLOCALONLY" = yes ]; then
	df -Ik -T local
else
	df -Ik
fi | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}' | EXCLMP="$EXCLMP" awk '
	BEGIN { n = split(ENVIRON["EXCLMP"], m, "\n"); for (i = 1; i <= n; i++) M[m[i]] = 1 }
	NR == 1 { hdr = $0; next }
	!(($1 " " $NF) in M) {
		# Header held back: with every row excluded the report must come out
		# empty - the server flags an empty disk report, a header-only one
		# reads as all-green.
		if (hdr != "") { print hdr; hdr = "" }
		print
	}
'

echo "[inode]"
# The System V df spells local-only "-l". It has to be guarded too: it stats
# every filesystem it reports, so leaving it open would only move the hang
# here from the disk report above.
if [ "$DFLOCALONLY" = yes ]; then
	/usr/sysv/bin/df -i -l
else
	/usr/sysv/bin/df -i
fi | sed -e 's!Mount Dir!Mount_Dir!' | awk '
NR<2 { hdr = sprintf("%-20s %10s %10s %10s %10s %s", $2, $5, $3, $4, $6, "Mounted on"); next }
$5>0 {
	# Header held back as in the disk report, for a smaller gain: an inode
	# section with no rows is green either way, unix_inode_report() exempting
	# an empty one for the Solaris host whose filesystems are all ZFS. What
	# the empty spelling buys is the server naming the reason ("No filesystems
	# reporting inode data") instead of echoing a header for zero filesystems.
	if (hdr != "") { print hdr; hdr = "" }
	printf "%-20s %10d %10d %10d %10s %s\n", $2, $5, $3, $4, $6, $1
}
'

echo "[mount]"
mount
echo "[realmem]"
lsattr -El sys0 -a realmem
echo "[freemem]"
vmstat 1 2 | tail -1
echo "[swap]"
lsps -s
echo "[ifconfig]"
ifconfig -a
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ports]"
netstat -an | grep "^tcp"
echo "[ifstat]"
netstat -v
echo "[ps]"
# I think the -f and -l options are ignored with -o, but this works...
ps -A -k -f -l -o pid,ppid,user,stat,pri,pcpu,time,etime,pmem,vsz,args

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
        $TOP -b 20
    fi
fi

# vmstat
nohup sh -c "vmstat 300 1 1>$XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ $XYMONTMP/xymon_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $XYMONTMP/xymon_vmstat.$MACHINEDOTS; rm -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; fi

exit

