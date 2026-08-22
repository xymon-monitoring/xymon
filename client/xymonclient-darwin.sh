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
# XYMONCLIENT_FS_REMOTE_DF_BUDGET: seconds to wait for the remote df probe
# before giving up for this cycle (default 30, capped at 3600; non-numeric,
# empty or zero -> 30). A poll BUDGET, not a kill timeout: a df wedged on a
# dead server sits in uninterruptible D-state where even SIGKILL is ignored,
# so it is left running and picked up on a later cycle rather than killed.
DFBUDGET="${XYMONCLIENT_FS_REMOTE_DF_BUDGET:-30}"
case "$DFBUDGET" in
	*[!0-9]*|'') DFBUDGET=30 ;;
	*[1-9]*) ;;
	*) DFBUDGET=30 ;;
esac
[ "$DFBUDGET" -le 3600 ] 2>/dev/null || DFBUDGET=3600

# XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES: the mount types whose stat() hard-blocks
# when the server is unreachable. Shipped as a built-in list so no site has to
# maintain one; assign the variable to override it.
: "${XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES=nfs nfs4 smbfs cifs afpfs webdav ceph glusterfs lustre afs}"
DFPROBEDIR="${XYMONTMP:-/tmp}"

# fs_mounts : one "TYPE<tab>MOUNTPOINT" line per mount, from mount(8). macOS
# mount(8) lists from getmntinfo(MNT_NOWAIT) -- verified in the shipped binary,
# three call sites, all MNT_NOWAIT -- so it never refreshes statfs and cannot
# block on a dead server, which df would.
fs_mounts() {
	mount | awk '
		{
			i = index($0, " on ")
			if (i == 0) next
			rest = substr($0, i + 4)
			if (match(rest, / \(/) == 0) next
			mp = substr(rest, 1, RSTART - 1)
			split(substr(rest, RSTART + 2), a, /[,)]/)
			printf "%s\t%s\n", a[1], mp
		}'
}

# fs_hardblocking : the mount points whose type hard-blocks, one per line.
fs_hardblocking() {
	fs_mounts | awk -F'\t' -v types="$XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES" '
		BEGIN { n = split(types, a, /[ \t]+/); for (i = 1; i <= n; i++) t[a[i]] = 1 }
		($1 in t) { print $2 }'
}

# fs_only IN EXCLUDE-LIST / fs_also IN KEEP-LIST : whole-line set operations, so
# a mount point containing a space is still matched exactly. The list is fed
# through the same stream as the input, ahead of a separator line, rather than
# through "awk -v": an assignment with embedded newlines is not portable, and
# on a BSD awk it silently yields an empty set.
fs_setop() {
	{ printf '%s\n' "$2"; echo '@@'; printf '%s\n' "$1"; } | awk -v want="$3" '
		$0 == "@@" { split_done = 1; next }
		!split_done { x[$0] = 1; next }
		$0 == "" { next }
		(($0 in x) ? "in" : "out") == want'
}
fs_only() { fs_setop "$1" "$2" out; }
fs_also() { fs_setop "$1" "$2" in; }

# probe_dir_is_local DIR : true when DIR is not itself on a hard-blocking
# filesystem. Decided purely from the mount list by longest mount-point prefix,
# and deliberately WITHOUT stat()ing DIR: if DIR were on the wedged mount, even
# `test -w DIR` would block in D-state inside the very fail-safe meant to
# prevent that. Symlinks are not resolved for the same reason (readlink would
# stat), so keep XYMONTMP on a real local path.
probe_dir_is_local()
{
	# The helper's status, not the pipeline's: a pipeline reports its last
	# command, and awk succeeds on no input at all -- which is what an
	# unreadable mount list looks like from there. Answering "local" then
	# sends the probe at a directory that may sit on the wedged mount this
	# exists to keep away from.
	_pdlm=$(fs_mounts) || return 1
	# dir via ENVIRON, not -v: -v escape-processes backslashes (gawk: \c -> c).
	printf '%s\n' "$_pdlm" | _pdldir="$1" awk -F'\t' -v types="$XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES" '
		BEGIN { dir = ENVIRON["_pdldir"] }
		BEGIN { n = split(types, a, /[ \t]+/); for (i = 1; i <= n; i++) t[a[i]] = 1 }
		{
			mp = $2
			if (dir == mp || mp == "/" || substr(dir, 1, length(mp) + 1) == mp "/") {
				if (length(mp) >= length(best)) { best = mp; besttype = $1 }
			}
		}
		END { exit (besttype in t) ? 1 : 0 }
	'
}

# fs_procname PID : the command name of a running process. ps is POSIX and
# reads process state only, so it cannot touch a wedged mount. Named so that
# df_sentinel() stays identical across the five clients, and so a test
# replaces the primitive rather than the code that uses it.
fs_procname()
{
	ps -o comm= -p "$1" 2>/dev/null | tr -d '[:space:]'
}

df_sentinel()
{
	_tag="$1"; shift
	_probe="$DFPROBEDIR/df-probe-$_tag"; _pidf="$_probe.pid"

	probe_dir_is_local "$DFPROBEDIR" || return 124

	# Claim the probe before starting it, or two cycles both find no pidfile
	# and both start a df -- and only the last pid written is recorded, so the
	# others pile up untracked, and these cannot be killed.
	#
	# The claim goes to a private file and is hard-linked into place: link
	# fails if the target exists, so one cycle wins, and what it publishes is
	# complete the moment it is visible. "set -C" gave exclusivity but not
	# that -- O_EXCL makes the create atomic, not the create-and-write, and a
	# reader in between saw an empty string, took it for a stale entry, and
	# started a probe on top of the first (@SoundGoof). Winning also proves
	# the directory is writable before any df runs.
	#
	# The redirect is wrapped in a subshell because a redirection error on a
	# special builtin kills the shell under dash instead of returning non-zero,
	# and then "return 124" never runs.
	#
	# The claim names the claiming shell and is replaced by df's pid below, so
	# a cycle killed in the gap leaves a dead pid, collected like any finished
	# probe, rather than a marker nothing clears.
	_tmpf="$_pidf.$$"
	if ! ( echo "claim:$$" > "$_tmpf" ) 2>/dev/null; then
		rm -f "$_tmpf"
		return 124
	fi
	# "-d" first: link into a directory puts the file *inside* it, so a
	# directory where the pidfile belongs would look like a won claim while
	# nothing recorded the pid. The old redirect simply failed there, and that
	# is the behaviour to keep: unclaimable means unavailable, not a probe
	# nobody tracks.
	if [ -d "$_pidf" ] || ! ln "$_tmpf" "$_pidf" 2>/dev/null; then
		_old=$(cat "$_pidf" 2>/dev/null)
		case "$_old" in
			claim:*)
				# Another cycle holds the claim and has not started its df yet.
				_c=${_old#claim:}
				[ -n "$_c" ] && kill -0 "$_c" 2>/dev/null && { rm -f "$_tmpf"; return 124; }
				;;
			?*)
				# A df we recorded. Still wedged? The command name is
				# checked so a recycled PID does not look like one.
				# fs_procname() is where each OS reads it -- /proc on Linux,
				# ps elsewhere -- and neither touches the filesystem, so a
				# wedged mount cannot block the check. Basename, because the
				# name is short on Linux and the BSDs, the full path on macOS.
				_c=$(fs_procname "$_old")
				_c=${_c##*/}
				if kill -0 "$_old" 2>/dev/null; then
					case "$_c" in
					  df) rm -f "$_tmpf"; return 124 ;;
					  "")
						# The two mistakes are not equally cheap. Restarting
						# a probe that is in fact still running leaves another
						# df on a dead server, and those cannot be killed;
						# keeping a mount unavailable for a cycle is visible
						# and bounded. So when the name cannot be read at all
						# -- no ps in a minimal container, no /proc, a pid
						# that just went away -- assume the probe is ours.
						echo "xymonclient: cannot read the command name of pid $_old; assuming the remote df is still running" >&2
						rm -f "$_tmpf"
						return 124
						;;
					esac
				fi
				;;
		esac
		# It finished after we stopped waiting. Its output is not usable: we
		# were not there to see the exit status, so a truncated file is
		# indistinguishable from a complete one, and its age is unknown.
		if [ ! -e "$_pidf" ]; then
			# The link failed with nothing in its way, so this filesystem does
			# not do hard links -- XYMONTMP pointed somewhere exotic. Every
			# cycle then reports the guarded mounts unavailable and never
			# probes: red rather than a false green, but saying nothing about
			# why. Say it.
			echo "xymonclient: cannot claim the remote df probe in $_pidf (does $DFPROBEDIR support hard links?)" >&2
			rm -f "$_tmpf"
			return 124
		fi
		# Discard and re-probe -- fresh data next cycle beats stale or partial
		# data now. One retry only: if another cycle claims it first, that
		# cycle owns the probe and this one waits its turn.
		rm -f "$_pidf" "$_probe"
		[ -d "$_pidf" ] && { rm -f "$_tmpf"; return 124; }
		ln "$_tmpf" "$_pidf" 2>/dev/null || { rm -f "$_tmpf"; return 124; }
	fi
	rm -f "$_tmpf"

	# Never run a synchronous remote df here: a foreground df would reintroduce
	# the hang this exists to prevent.
	( : > "$_probe" ) 2>/dev/null || { rm -f "$_pidf"; return 124; }
	df "$@" > "$_probe" 2>/dev/null &
	_pid=$!
	# Publish the pid the same way the claim was published: a plain
	# "> $_pidf" truncates before it writes, and a cycle reading in that
	# interval sees an empty file, removes it, and starts a second probe --
	# leaving this one's df running with nothing recording it (@SoundGoof).
	# A rename within one directory replaces the claim in a single step, so a
	# reader gets either the whole claim or the whole pid.
	if ! ( printf '%s\n' "$_pid" > "$_tmpf" ) 2>/dev/null || ! mv -f "$_tmpf" "$_pidf" 2>/dev/null; then
		rm -f "$_tmpf"
		# The pre-flight passed and this still failed, so a df is running that
		# the next cycle cannot recognise. The claim is still in place and this
		# shell outlives the budget, so no second probe starts meanwhile. Say
		# so: an operator seeing repeated unavailable rows needs to know a df
		# may be outstanding.
		echo "xymonclient: cannot record remote df pid in $_pidf" >&2
	fi
	_n=0
	while kill -0 "$_pid" 2>/dev/null; do
		[ "$_n" -ge "$DFBUDGET" ] && break
		sleep 1; _n=$((_n + 1))
	done
	if kill -0 "$_pid" 2>/dev/null; then
		return 124			# budget spent; leave it running as the sentinel
	fi
	wait "$_pid"; _rc=$?
	rm -f "$_pidf"
	if [ "$_rc" -ne 0 ]; then rm -f "$_probe"; return 124; fi
	sed -e '1d' "$_probe"; rm -f "$_probe"
	return 0
}

# fs_guarded TAG FLAGS MOUNTS : rows for the hard-blocking set, behind the
# sentinel. Prints nothing when there is nothing to probe; on an unanswering
# server prints one 100%-full marker row per mount rather than dropping it,
# since the server reads an absent filesystem as green -- one filesystem red
# instead of the whole host purple.
fs_guarded() {
	_tag="$1"; _flags="$2"; _mounts="$3"
	[ -n "$_mounts" ] || return 0
	set -f
	_oifs=$IFS
	# The two lists split differently, and the caller has already set IFS to
	# newline for the mount points: taking that for the flags too would hand df
	# the single unknown option "-P -H", and every guarded mount would report
	# unavailable while its server was answering perfectly.
	IFS=' '
	# shellcheck disable=SC2086
	set -- $_flags
	# The mount points on newline only, so one containing a space stays a
	# single argument.
	IFS='
'
	# shellcheck disable=SC2086
	set -- "$@" $_mounts
	_rout=`df_sentinel "$_tag" "$@"`
	if [ $? -eq 124 ]; then
		for _m in $_mounts; do
			if [ "$_tag" = inode ]; then
				echo "unavailable:$_m 1 1 0 100% 1 0 100% $_m"
			else
				echo "unavailable:$_m 1 1 0 100% $_m"
			fi
		done
	else
		printf '%s\n' "$_rout"
	fi
	IFS=$_oifs
	set +f
}

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
# The hard-blocking mounts are collected separately, behind the sentinel: a df
# on a dead NFS/SMB server sits in uninterruptible D-state where even SIGKILL is
# ignored, so the per-path loop below must never be handed one (#316).
FS_HARD_ALL=`fs_hardblocking`
FILESYSTEMS_REMOTE=`fs_also "$FILESYSTEMS" "$FS_HARD_ALL"`
FILESYSTEMS=`fs_only "$FILESYSTEMS" "$FS_HARD_ALL"`
# apfs is excluded from the inode report: its ifree is derived from the shared
# container free space (identical across a container's volumes, %iused pinned
# at 0%), so the numbers carry no exhaustion signal - the ZFS situation,
# measured on a real Mac. On an all-APFS system this list is empty and the
# [inode] section legitimately so: "nothing inode-limited to monitor" is data,
# not a failure.
FILESYSTEMS_INODE=`fs_list apfs`
FILESYSTEMS_INODE_REMOTE=`fs_also "$FILESYSTEMS_INODE" "$FS_HARD_ALL"`
FILESYSTEMS_INODE=`fs_only "$FILESYSTEMS_INODE" "$FS_HARD_ALL"`
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
if [ -n "$FILESYSTEMS" ] || [ -n "$FILESYSTEMS_REMOTE" ]; then
	# Render the table by probing each path; emit_df turns empty output (every
	# probe failed) into a failure marker. A partial run still prints rows. The
	# hard-blocking mounts are appended from the sentinel, headerless -- and
	# when they are the only ones left, the header has to come from somewhere,
	# so it is taken from / (the boot volume, which is never a remote mount).
	_out=$( (IFS=$'\n'
	 set -f		# a mountpoint containing a glob char must not expand
	 if [ -n "$FILESYSTEMS" ]; then
	   set $FILESYSTEMS
	   df -P -H $1; shift
	   while test $# -gt 0
	   do
	     df -P -H $1 | tail -1 | sed 's/\([^ ]\) \([^ ]\)/\1_\2/g'
	     shift
	   done
	 elif [ -n "$FILESYSTEMS_REMOTE" ]; then
	   df -P -H / | head -1
	 fi
	 fs_guarded disk "-P -H" "$FILESYSTEMS_REMOTE") | column -t -s " " | sed -e 's!Mounted *on!Mounted on!' )
else
	echo "xymonclient-darwin: no filesystems survived the mount filter; check XYMONCLIENT_FS_* settings" >&2
	_out=""
fi
emit_df disk Disk

echo "[inode]"
# Empty list here is legitimate (all-APFS Mac: nothing inode-limited exists),
# so unlike the disk report there is no marker - the section stays empty.
if [ -n "$FILESYSTEMS_INODE" ] || [ -n "$FILESYSTEMS_INODE_REMOTE" ]; then
	_out=$( (IFS=$'\n'
	 set -f		# a mountpoint containing a glob char must not expand
	 if [ -n "$FILESYSTEMS_INODE" ]; then
	   set $FILESYSTEMS_INODE
	   df -P -i $1; shift
	   while test $# -gt 0
	   do
	     df -P -i $1 | tail -1 | sed 's/\([^0123456789% ]\) \([^ ]\)/\1_\2/g'
	     shift
	   done
	 elif [ -n "$FILESYSTEMS_INODE_REMOTE" ]; then
	   df -P -i / | head -1
	 fi
	 fs_guarded inode "-P -i" "$FILESYSTEMS_INODE_REMOTE") | awk '
NR<2{printf "%-20s %10s %10s %10s %10s %s\n", $1, "itotal", $6, $7, $8, $9}
(NR>=2 && $6>0) {printf "%-20s %10d %10d %10d %10s %s\n", $1, $6+$7, $6, $7, $8, $9}' )
	emit_df inode Inode
fi

echo "[mount]"
mount
echo "[meminfo]"
vm_stat
# The kernel's own memory-health verdict (1=normal, 2=warn, 4=critical)
# drives a darwin-only "swap" status server-side. macOS swap capacity is
# dynamic (the swapfile set grows on demand), so used/total percentages
# carry no signal; vm.swapusage rides along as trend data only.
sysctl kern.memorystatus_vm_pressure_level 2>/dev/null
sysctl vm.swapusage 2>/dev/null
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

