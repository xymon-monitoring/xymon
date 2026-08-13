#!/bin/sh
#----------------------------------------------------------------------------#
# NetBSD client for Xymon                                                    #
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
# Exclude FS types via df "-t no<csv>": defaults minus INCLUDE_TYPES, plus
# EXCLUDE_TYPES. Remote mounts are controlled separately with df -l.
: "${XYMONCLIENT_FS_INCLUDE_TYPES=}"
: "${XYMONCLIENT_FS_EXCLUDE_TYPES=}"
fs_excl_opt() {
    case $- in *f*) _restoreglob=no ;; *) _restoreglob=yes; set -f ;; esac
    _l=""
    for _t in kernfs procfs cd9660 null ptyfs "$@"; do
        for _i in $XYMONCLIENT_FS_INCLUDE_TYPES; do [ "$_t" = "$_i" ] && continue 2; done
        case " $_l " in *" $_t "*) ;; *) _l="$_l $_t" ;; esac
    done
    for _t in $XYMONCLIENT_FS_EXCLUDE_TYPES; do
        case " $_l " in *" $_t "*) ;; *) _l="$_l $_t" ;; esac
    done
    _csv=`echo $_l | tr ' ' ','`
    [ "$_restoreglob" = yes ] && set +f
    [ -n "$_csv" ] && printf -- '-tno%s' "$_csv"
}
DFLOCALONLY="${XYMONCLIENT_FS_DF_LOCAL_ONLY:-yes}"
case "$DFLOCALONLY" in
    yes|no) ;;
    *) echo "xymonclient-netbsd: invalid XYMONCLIENT_FS_DF_LOCAL_ONLY '$DFLOCALONLY', using yes" >&2; DFLOCALONLY=yes ;;
esac
DFLOCAL=""; [ "$DFLOCALONLY" = yes ] && DFLOCAL="-l"
run_df() {
    _flag="$1"; shift
    _excl=`fs_excl_opt "$@"`
    df "$_flag" $DFLOCAL $_excl
}
emit_df() {
    _kind="$1"; _label="$2"; shift 2
    _out=`run_df "$@"`; _rc=$?
    if [ -z "$_out" ] && [ "$_rc" -ne 0 ]; then
        echo "xymonclient-netbsd: df $_kind collection failed (status $_rc) with no output; reporting data as unavailable" >&2
        echo "$_label report collection failed: df exited $_rc with no output"
        return 1
    fi
    return 0
}
if emit_df disk Disk -P; then
    printf '%s\n' "$_out" | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
fi
echo "[inode]"
if emit_df inode Inode -i zfs; then
    printf '%s\n' "$_out" | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}' | awk '
NR == 1 { print "Filesystem itotal iused ifree %iused Mounted on"; next }
($6 + $7) <= 0 { next }
{ printf "%s %d %d %d %s %s\n", $1, $6+$7, $6, $7, $8, $9 }
'
fi
echo "[mount]"
mount
echo "[meminfo]"
$XYMONHOME/bin/netbsd-meminfo
echo "[swapctl]"
/sbin/swapctl -s
echo "[ifconfig]"
ifconfig -a
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ifstat]"
netstat -i -b -n | egrep -v "^lo|<Link"
echo "[ports]"
(netstat -na -f inet; netstat -na -f inet6) | grep "^tcp"
echo "[ps]"
ps -ax -ww -o pid,ppid,user,start,state,pri,pcpu,cputime,pmem,rss,vsz,args

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[nproc]"
        sysctl -n hw.ncpu
        echo "[top]"
	$TOP -n 20
    fi
fi

# vmstat
nohup sh -c "vmstat 300 2 1>$XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ $XYMONTMP/xymon_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $XYMONTMP/xymon_vmstat.$MACHINEDOTS; rm -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; fi

exit

