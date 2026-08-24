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
: "${XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES=nfs nfs4 smbfs cifs ceph glusterfs puffs|nfs lustre afs}"
DFPROBEDIR="${XYMONTMP:-/tmp}"

# fs_mounts : one "TYPE<tab>MOUNTPOINT<tab>DEVICE" line per mount, from mount(8).
# NetBSD mount(8) lists from getmntinfo(MNT_NOWAIT), so it never refreshes
# statfs and cannot block on a dead server.
# df -P would, which is the whole reason this list is read here instead.
fs_mounts() {
	mount | awk '
		{
			i = index($0, " on ")
			if (i == 0) next
			rest = substr($0, i + 4)
			if (match(rest, / type [^ ]+ \(/) == 0) next
			mp = substr(rest, 1, RSTART - 1)
			type = substr(rest, RSTART + 6)
			sub(/ \(.*/, "", type)
			dev = substr($0, 1, i - 1)
			printf "%s\t%s\t%s\n", type, mp, dev
		}'
}

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

# run_df FLAG [extra-excludes...]: df rows for one report behind the FS filter.
# With DF_LOCAL_ONLY=no the local and remote sets are collected separately, so a
# wedged remote server can neither block nor stale the local data.
run_df() {
	_flag="$1"; shift
	if [ "$DFLOCALONLY" = yes ]; then
		df "$_flag" $DFLOCAL `fs_excl_opt "$@"`
		return
	fi

	# Everything that cannot hard-block, in one plain df. "-t no<csv>" filters
	# the mount list before df stat()s anything, exactly as -l does, so
	# excluding the hard-blocking types is enough to keep this call safe -- and
	# unlike -l it still reports healthy remote filesystems, which is what
	# LOCAL_ONLY=no asks for. Its status is kept: emit_df turns "no output and
	# non-zero" into the failure marker that drives the server yellow, and
	# splitting the collection must not cost that.
	_plain=`df "$_flag" \`fs_excl_opt "$@" $XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES\``
	_prc=$?
	[ -n "$_plain" ] && printf '%s\n' "$_plain"

	# Remote set: the hard-blocking mounts, from mount(8) (which never
	# refreshes statfs), minus any the admin excluded -- naming a mount whose
	# type is excluded gives a df with nothing to process, which exits nonzero
	# and would report every one of those healthy mounts unavailable.
	_exl=`fs_excl_opt "$@" | sed -e 's/^-tno//' -e 's/,/ /g'`
	_rm=`fs_mounts | awk -F'\t' -v types="$XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES" -v excl="$_exl" '
		BEGIN {
			n = split(types, a, /[ \t]+/); for (i = 1; i <= n; i++) t[a[i]] = 1
			m = split(excl,  b, /[ \t]+/); for (i = 1; i <= m; i++) x[b[i]] = 1
		}
		($1 in t) && !($1 in x) { print $2 }'`
	[ -n "$_rm" ] || return $_prc

	case $- in *f*) _rg=no ;; *) _rg=yes; set -f ;; esac
	_oifs=$IFS; IFS='
'
	# Deliberate split on newline only, so a mount point containing a space
	# stays one argument. set -f above keeps globbing out of it.
	set -- "$_flag"
	# shellcheck disable=SC2086
	for _m in $_rm; do set -- "$@" "$_m"; done
	_tag=disk; [ "$_flag" = -i ] && _tag=inode
	_rout=`df_sentinel "$_tag" "$@"`
	if [ $? -eq 124 ]; then
		# Unavailable: surface each remote mount as a failed (100%) row rather
		# than dropping it, since the server reads an absent filesystem as
		# green. This turns one filesystem red instead of purpling the host.
		# The row names the server. df could not answer, but the mount table
		# still knows which device is behind the mount point, and that is the
		# first thing an operator needs. The sizes are reported as "-": they
		# were not measured, and a number here would be trended as a reading.
		# The capacity stays 100% because that is what turns the column red.
		# The inode report is read column-wise (iused, ifree and %iused are
		# fields 6-8), so its marker row carries those columns too.
		# The mount list rides the mount-table stream behind an @@ separator:
		# a -v assignment cannot carry newlines on BSD awk (fs_setop makes
		# the same move). A spaced device is re-encoded (\040) to keep the
		# column count.
		{ fs_mounts; echo '@@'; printf '%s\n' "$_rm"; } | awk -F'\t' -v inode="$_flag" '
			$0 == "@@" { rm = 1; next }
			!rm { dev[$2] = ($3 == "" ? "-" : $3); next }
			$0 == "" { next }
			{
				d = ($0 in dev) ? dev[$0] : "-"
				n = split(d, dp, / /)
				if (n > 1) { d = dp[1]; for (j = 2; j <= n; j++) d = d "\\040" dp[j] }
				if (inode == "-i") printf "%s - - - 100%% - - 100%% %s\n", d, $0
				else printf "%s - - - 100%% %s\n", d, $0
			}'
	else
		printf '%s\n' "$_rout"
	fi
	IFS=$_oifs
	[ "$_rg" = yes ] && set +f
	return 0
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
$6 == "-" { printf "%s %s %s %s %s %s\n", $1, "-", "-", "-", $8, $9; next }
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

