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
