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
# on top of the nodev default. Defaults to "iso9660 squashfs fuse.snapfuse" --
# read-only images reported 100% full by design (snaps mount as squashfs, or as
# fuse.snapfuse where snapd falls back to FUSE), none a nodev type, so they must
# be named here. Matching is on the exact df type token; nodev types (overlay,
# bare fuse, ...) are already excluded, so other effective entries name a
# non-nodev type, e.g. adding "fuse.sshfs vfat" to drop a specific FUSE subtype
# and a device-backed mount. Set to "" to monitor these too.
: "${XYMONCLIENT_FS_EXCLUDE_TYPES=iso9660 squashfs fuse.snapfuse}"
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

# XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES: the df types whose stat() hard-blocks
# when the server is unreachable. Shipped as a built-in list so no site has to
# maintain one; assign the variable to override it. Only consulted with
# DF_LOCAL_ONLY=no, since df -l keeps these mounts out entirely.
: "${XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES=nfs nfs4 cifs smb3 ceph glusterfs fuse.glusterfs lustre afs}"
DFPROBEDIR="${XYMONTMP:-/tmp}"

# probe_dir_is_local DIR : true when DIR is not itself on a hard-blocking
# filesystem. Decided purely from /proc/mounts (reading it never blocks) by
# longest mount-point prefix, and deliberately WITHOUT stat()ing DIR: if DIR
# were on the wedged mount, even `test -w DIR` would block in D-state inside
# the very fail-safe meant to prevent that. Symlinks are not resolved for the
# same reason (readlink would stat), so keep XYMONTMP on a real local path.
probe_dir_is_local()
{
	[ -r /proc/mounts ] || return 1
	awk -v dir="$1" -v types="$XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES" '
		BEGIN { n = split(types, a, /[ \t]+/); for (i = 1; i <= n; i++) t[a[i]] = 1 }
		{
			mp = $2; gsub(/\\040/, " ", mp)
			if (dir == mp || mp == "/" || substr(dir, 1, length(mp) + 1) == mp "/") {
				if (length(mp) >= length(best)) { best = mp; besttype = $3 }
			}
		}
		END { exit (besttype in t) ? 1 : 0 }
	' /proc/mounts
}

# df_sentinel TAG ARGS... : df for the remote set with at most ONE outstanding
# probe per TAG. Prints rows (header stripped); returns 0 with data, 124 when
# unavailable. A wedged df cannot be killed, so it is left running and detected
# on the next cycle through a pidfile -- orphans never accumulate, and it
# recovers by itself once the server returns.
df_sentinel()
{
	_tag="$1"; shift
	_probe="$DFPROBEDIR/df-probe-$_tag"; _pidf="$_probe.pid"

	probe_dir_is_local "$DFPROBEDIR" || return 124

	# Claim the probe before starting it, atomically. Testing for the pidfile
	# and creating it afterwards let two client cycles both find it absent and
	# both start a df; only the last pid written was ever recorded, so the
	# other probe was untracked from then on and more piled up every cycle -
	# and these are the processes that cannot be killed. Under "set -C" the
	# create is O_EXCL, so exactly one cycle wins, and winning doubles as the
	# proof that the pidfile is writable before any df starts.
	#
	# A subshell, not a bare ":": ":" is a POSIX special builtin, and a
	# redirection error on one is fatal to the shell rather than a non-zero
	# status. Under dash - /bin/sh on Debian and Ubuntu - "return 124" would
	# never run, the sentinel would die with status 2, and run_df would emit
	# nothing for the remote set instead of the unavailable rows. It also keeps
	# noclobber out of the caller.
	#
	# The claim names the claiming shell and is replaced by df's pid below, so
	# a cycle killed in the gap leaves a pid that is simply dead and is
	# collected like any finished probe, rather than a marker nothing clears.
	if ! ( set -C; echo "claim:$$" > "$_pidf" ) 2>/dev/null; then
		_old=$(cat "$_pidf" 2>/dev/null)
		case "$_old" in
			claim:*)
				# Another cycle holds the claim and has not started its df yet.
				_c=${_old#claim:}
				[ -n "$_c" ] && kill -0 "$_c" 2>/dev/null && return 124
				;;
			?*)
				# A df we recorded. Still wedged? The command name is checked
				# so a recycled PID does not look like one. Read it with ps,
				# not /proc/PID/comm: ps is POSIX, gives the same answer, is
				# already a dependency of this client (the [ps] section), and
				# reads process state only -- it cannot touch the wedged mount.
				# Take the basename: comm is the short name on Linux and the
				# BSDs, but the full path on macOS.
				_c=$(ps -o comm= -p "$_old" 2>/dev/null | tr -d '[:space:]')
				_c=${_c##*/}
				if kill -0 "$_old" 2>/dev/null && [ "$_c" = df ]; then
					return 124
				fi
				;;
		esac
		# It finished after we stopped waiting. Its output is not usable: we
		# were not there to see the exit status, so a truncated file is
		# indistinguishable from a complete one, and its age is unknown.
		# Discard and re-probe -- fresh data next cycle beats stale or partial
		# data now. One retry only: if another cycle claims it first, that
		# cycle owns the probe and this one waits its turn.
		rm -f "$_pidf" "$_probe"
		( set -C; echo "claim:$$" > "$_pidf" ) 2>/dev/null || return 124
	fi

	# Never run a synchronous remote df here: a foreground df would reintroduce
	# the hang this exists to prevent.
	( : > "$_probe" ) 2>/dev/null || { rm -f "$_pidf"; return 124; }
	df "$@" > "$_probe" 2>/dev/null &
	_pid=$!
	if ! echo "$_pid" > "$_pidf"; then
		# The pre-flight passed and this still failed, so a df is running that
		# the next cycle cannot recognise. Say so: an operator seeing repeated
		# unavailable rows needs to know a df may be outstanding.
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

# run_df INODEFLAG : emit df -P output (header + rows). With DF_LOCAL_ONLY=no
# the local and remote sets are collected separately, so a wedged remote server
# can neither block nor stale the local data (df -l cannot stat a remote server).
run_df()
{
	DFINODES="$1"
	set -- -P
	[ "$DFINODES" = yes ] && set -- "$@" -i

	case $- in
		*f*) DFRESTOREGLOB=no ;;
		*) DFRESTOREGLOB=yes; set -f ;;
	esac
	for t in $EXCLUDES; do
		set -- "$@" -x "$t"
	done
	[ "$DFRESTOREGLOB" = yes ] && set +f

	if [ "$DFLOCALONLY" = yes ]; then
		df "$@" -l
		return
	fi

	# Everything that cannot hard-block, in one plain df. -x filters the mount
	# list before df stat()s anything, exactly as -l does, so excluding the
	# hard-blocking types is enough to keep this call safe - and unlike -l it
	# still reports healthy remote filesystems, which is what LOCAL_ONLY=no
	# asks for. The hard-block list decides what needs the sentinel below, not
	# what is visible.
	# The hard-blocking exclusions belong to this call and no other. Adding them
	# to "$@" left them in front of the mount points handed to the guarded
	# probe below, and an exclusion applies to an explicitly named operand too:
	# df processed no filesystem at all, exited nonzero, and every one of those
	# healthy mounts came out unavailable and 100% full. The subshell gets a
	# copy of the positional parameters, so they stay here.
	# Its status is kept: emit_df turns "no output and non-zero" into the
	# failure marker that drives the server yellow, and splitting the
	# collection must not cost that. Returning 0 unconditionally here left a
	# failing df with no rows looking like a healthy empty report -- the same
	# false green the marker exists to prevent, reached from the other side.
	_plain=$(
		case $- in *f*) ;; *) set -f ;; esac
		for t in $XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES; do
			set -- "$@" -x "$t"
		done
		df "$@"
	)
	_prc=$?
	[ -n "$_plain" ] && printf '%s\n' "$_plain"

	# Remote set: pick the hard-blocking mounts out of /proc/mounts (reading it
	# never blocks), then probe them behind the sentinel. Types are matched by
	# exact set membership rather than a pattern, so "fuse.glusterfs" compares
	# literally. Mount points are \040-decoded, and iterated with IFS=newline
	# so that one containing a space stays a single argument.
	# A type the admin excluded is not collected, guarded or not: its "-x" is in
	# "$@" and would exclude the very mount named beside it, which is the same
	# empty-and-nonzero df as above, reported as unavailable rather than simply
	# left out.
	# From here on the plain df's status is what a caller sees when nothing at
	# all was collected: no local rows, and no remote set to fall back on.
	[ -r /proc/mounts ] || return $_prc
	_rm=$(awk -v types="$XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES" -v excl="$EXCLUDES" '
		BEGIN {
			n = split(types, a, /[ \t]+/); for (i = 1; i <= n; i++) t[a[i]] = 1
			m = split(excl,  b, /[ \t]+/); for (i = 1; i <= m; i++) x[b[i]] = 1
		}
		($3 in t) && !($3 in x) { mp = $2; gsub(/\\040/, " ", mp); print mp }' /proc/mounts)
	[ -n "$_rm" ] || return $_prc

	case $- in *f*) _rg=no ;; *) _rg=yes; set -f ;; esac
	_oifs=$IFS; IFS='
'
	# Deliberate split on newline only, so a mount point containing a space
	# stays one argument. set -f above keeps globbing out of it.
	# shellcheck disable=SC2086
	for _m in $_rm; do set -- "$@" "$_m"; done
	_tag=disk; [ "$DFINODES" = yes ] && _tag=inode
	_rout=$(df_sentinel "$_tag" "$@")
	if [ $? -eq 124 ]; then
		# Unavailable: surface each remote mount as a failed (100%) row rather
		# than dropping it, since the server reads an absent filesystem as
		# green. This turns one filesystem red instead of purpling the host.
		for _m in $_rm; do
			echo "unavailable:$_m 1 1 0 100% $_m"
		done
	else
		printf '%s\n' "$_rout"
	fi
	IFS=$_oifs
	[ "$_rg" = yes ] && set +f
	return 0
}
# emit_df INODEFLAG LABEL
# Run df (optionally in inode mode) and reproduce the historical sed join.
# The server reads an empty section as green, so a failed df must not pass
# silently: on any nonzero exit with no output, emit a failure marker (no
# recognisable df header) to drive the server yellow. A nonzero exit that still
# prints mounts (e.g. one unreadable mount) and a clean empty exit 0 (Solaris
# all-ZFS inodes) keep their output unchanged.
emit_df()
{
	DFOUT=`run_df "$1"`
	DFRC=$?
	if [ -z "$DFOUT" ]; then
		[ "$DFRC" -eq 0 ] && return
		echo "xymonclient-linux: df $2 collection failed (status $DFRC) with no output; reporting data as unavailable" >&2
		echo "$2 collection failed: df exited $DFRC with no output"
		return
	fi
	# Inode report ("$1" = yes) only: drop filesystems with no inode limit. df
	# prints "-" in the IUse% column (field 5) for them (btrfs, zfs, 9p, many
	# fuse); they can never run out of inodes, so the row is noise and may carry
	# bogus counts (e.g. a negative IUsed on 9p). The header (NR==1) is kept; for
	# the disk report the awk is a pass-through. (awk is already required above,
	# so this adds no new dependency.)
	printf '%s\n' "$DFOUT" | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}' -e "s&^rootfs&${ROOTFS}&" \
	| awk -v ino="$1" 'NR == 1 || ino != "yes" || $5 != "-"'
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
