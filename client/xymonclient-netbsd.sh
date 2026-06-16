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
# Exclude FS types via df "-t no<csv>": a default set, minus INCLUDE_TYPES, plus
# EXCLUDE_TYPES. Remote (nfs) mounts are hidden by df -l (DF_LOCAL_ONLY=yes), not
# by type, so DF_LOCAL_ONLY=no can surface them.
: "${XYMONCLIENT_FS_INCLUDE_TYPES=}"
: "${XYMONCLIENT_FS_EXCLUDE_TYPES=}"
fs_excl_opt() {
	# noglob: treat a type like "tmp*" as a literal token, not a filename glob.
	case $- in *f*) _restoreglob=no ;; *) _restoreglob=yes; set -f ;; esac
	_l=""
	# Default excludes, minus any INCLUDE_TYPES.
	for _t in kernfs procfs cd9660 null; do
		for _i in $XYMONCLIENT_FS_INCLUDE_TYPES; do [ "$_t" = "$_i" ] && continue 2; done
		case " $_l " in *" $_t "*) ;; *) _l="$_l $_t" ;; esac
	done
	# EXCLUDE_TYPES last, so a type in both lists stays excluded (EXCLUDE wins).
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
# run_df FLAG [extra-excludes...]: df rows for one report behind the FS filter
# and local-only flag. The seam where the remote-df sentinel will hook.
run_df() {
	_flag="$1"; shift
	_excl=`fs_excl_opt "$@"`
	df "$_flag" $DFLOCAL $_excl
}
# emit_df KIND LABEL FLAG [extra-excludes...]: run_df + failure guard. On a
# non-zero df with no output, print a one-line marker (no df header) so the
# server goes yellow not green, and return 1 so the caller skips formatting; else
# leave the rows in $_out and return 0 to format. A non-zero df that still prints
# mounts flows through unchanged.
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
# The sed joins lines df split in two.
if emit_df disk Disk -P; then
	printf '%s\n' "$_out" | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
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

