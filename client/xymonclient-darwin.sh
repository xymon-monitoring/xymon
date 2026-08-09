#!/bin/sh
#
#----------------------------------------------------------------------------#
# Darwin (Mac OS X) client for Xymon                                         #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id$

# Use LANG=C, since some locales have different numeric delimiters
# causing the Xymon load-average calculation to fail
LANG=C
export LANG

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
# macOS df selects per path, not with -t, so the filter is applied to the
# mount(8) list: drop the noise nobrowse/read-only attributes plus EXCLUDE_TYPES,
# keep INCLUDE_TYPES.
: "${XYMONCLIENT_FS_INCLUDE_TYPES=}"
: "${XYMONCLIENT_FS_EXCLUDE_TYPES=}"
# DF_LOCAL_ONLY: "yes" (default) keeps only mounts mount(8) flags "local"
# (MNT_LOCAL, what df -l selects elsewhere), hiding remote mounts (nfs, smbfs,
# afs, ...); "no" surfaces them; invalid warns and uses yes. (df is per-path
# here, where -l is ignored, so we filter mount(8) output instead.)
DFLOCALONLY="${XYMONCLIENT_FS_DF_LOCAL_ONLY:-yes}"
case "$DFLOCALONLY" in
	yes|no) ;;
	*) echo "xymonclient-darwin: invalid XYMONCLIENT_FS_DF_LOCAL_ONLY '$DFLOCALONLY', using yes" >&2; DFLOCALONLY=yes ;;
esac
# Escape ERE metachars (and the sed delimiter) so a type is matched literally in
# the mount(8) filter, not as a regex (e.g. "fuse.*" must not match "fuseblk").
_re_escape() { printf '%s' "$1" | sed 's,[]\^$.*+?()[{}|/],\\&,g'; }
# noglob: treat a token like "tmp*" literally, not as a filename glob.
case $- in *f*) _restoreglob=no ;; *) _restoreglob=yes; set -f ;; esac
_dflt=""
# Always-drop attributes (apply to local volumes too, so independent of
# LOCAL_ONLY), minus INCLUDE_TYPES. Remote mounts are handled by the local filter.
# Modern macOS (Catalina+) seals / read-only and flags every system volume -
# including the data volume holding all user files - "nobrowse". Apple marks
# that one "root data": it is exempted from these DEFAULT drops, so the one
# volume that can actually fill is always reported. (The sealed read-only root
# cannot fill and stays dropped; writes land on the data volume.)
for _t in nobrowse read-only; do
	for _i in $XYMONCLIENT_FS_INCLUDE_TYPES; do [ "$_t" = "$_i" ] && continue 2; done
	_dflt="$_dflt|`_re_escape "$_t"`"
done
# EXCLUDE_TYPES are applied separately and unconditionally: an explicit admin
# exclude beats both INCLUDE_TYPES (EXCLUDE wins) and the root-data exemption.
_user=""
for _t in $XYMONCLIENT_FS_EXCLUDE_TYPES; do
	_user="$_user|`_re_escape "$_t"`"
done
[ "$_restoreglob" = yes ] && set +f
_dflt="${_dflt#|}"
_user="${_user#|}"
# Mirror df -l: with LOCAL_ONLY=yes keep only mounts flagged "local" (MNT_LOCAL),
# dropping remote mounts regardless of type.
_localfilter=""
[ "$DFLOCALONLY" = yes ] && _localfilter='/[(, ]local[,) ]/!d;'
# fs_list [extra-type-excludes...]: mount points surviving the filter, one per
# line. Extra excludes are per-report (apfs for the inode report) and applied
# like EXCLUDE_TYPES (unconditional).
fs_list() {
	_prog="$_localfilter"
	[ -n "$_dflt" ] && _prog="$_prog/[(, ]root data[,)]/!{/[\( ]($_dflt)[ ,\)]/d;};"
	_x="$_user"
	for _t; do _x="$_x|`_re_escape "$_t"`"; done
	_x="${_x#|}"
	[ -n "$_x" ] && _prog="$_prog/[\( ]($_x)[ ,\)]/d;"
	mount | sed -E "${_prog}s/^.* on (.*) \(.*$/\1/"
}
FILESYSTEMS=`fs_list`
# apfs is excluded from the inode report: its ifree is derived from the shared
# container free space (identical across a container's volumes, %iused pinned
# at 0%), so the numbers carry no exhaustion signal - the ZFS situation,
# measured on a real Mac. On an all-APFS system this list is empty and the
# [inode] section legitimately so: "nothing inode-limited to monitor" is data,
# not a failure.
FILESYSTEMS_INODE=`fs_list apfs`
# emit_df KIND LABEL: print the table in $_out, or -- when df produced nothing --
# a one-line marker (no df header) so the server goes yellow not green. macOS df
# is queried per path (into $_out above), so this only applies the guard; that
# per-path loop is the seam where the remote-df sentinel will route non-local mounts.
emit_df() {
	if [ -n "$_out" ]; then
		printf '%s\n' "$_out"
		return
	fi
	echo "xymonclient-darwin: df $1 collection failed with no output; reporting data as unavailable" >&2
	echo "$2 report collection failed: df produced no output"
}
# Never run df with an empty list: no path means "report every mount", which
# would defeat the filter. An empty list is itself reportable - if a macOS
# change (or a config mistake) filters everything away, the marker turns the
# column yellow instead of silently blanking disk monitoring.
if [ -n "$FILESYSTEMS" ]; then
	# Render the table by probing each path; emit_df turns empty output (every
	# probe failed) into a failure marker. A partial run still prints rows.
	_out=$( (IFS=$'\n'
	 set -f		# a mountpoint containing a glob char must not expand
	 set $FILESYSTEMS
	 df -P -H $1; shift
	 while test $# -gt 0
	 do
	   df -P -H $1 | tail -1 | sed 's/\([^ ]\) \([^ ]\)/\1_\2/g'
	   shift
	 done) | column -t -s " " | sed -e 's!Mounted *on!Mounted on!' )
else
	echo "xymonclient-darwin: no filesystems survived the mount filter; check XYMONCLIENT_FS_* settings" >&2
	_out=""
fi
emit_df disk Disk

echo "[inode]"
# Empty list here is legitimate (all-APFS Mac: nothing inode-limited exists),
# so unlike the disk report there is no marker - the section stays empty.
if [ -n "$FILESYSTEMS_INODE" ]; then
	_out=$( (IFS=$'\n'
	 set -f		# a mountpoint containing a glob char must not expand
	 set $FILESYSTEMS_INODE
	 df -P -i $1; shift
	 while test $# -gt 0
	 do
	   df -P -i $1 | tail -1 | sed 's/\([^0123456789% ]\) \([^ ]\)/\1_\2/g'
	   shift
	 done) | awk '
NR<2{printf "%-20s %10s %10s %10s %10s %s\n", $1, "itotal", $6, $7, $8, $9}
(NR>=2 && $6>0) {printf "%-20s %10d %10d %10d %10s %s\n", $1, $6+$7, $6, $7, $8, $9}' )
	emit_df inode Inode
fi

echo "[mount]"
mount
echo "[meminfo]"
vm_stat
echo "[ifconfig]"
ifconfig -a
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ifstat]"
netstat -ibn | egrep -v "^lo|<Link"
echo "[ports]"
netstat -an | grep -e "^tcp" -e "^udp"
echo "[ps]"
ps -ax -ww -o pid,ppid,user,start,state,pri,pcpu,time,pmem,rss,vsz,command

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != "" -a "$AWK" != ""
then
    if test -x "$TOP" -a -x "$AWK"
    then
        echo "[nproc]"
        sysctl -n hw.ncpu
        echo "[top]"
	$TOP -l 2 -n 20 -o cpu | $AWK '/^Processes:/ {toprun++} toprun == 2'
    fi
fi

exit

