# emit_df KIND LABEL FLAG [extra-excludes...]: run_df + failure guard. Leaves
# the rows in $_out and returns 0 to format them; returns 1 when there is
# nothing to format. $FS_SELF names this client in the message on stderr.
emit_df() {
	_kind="$1"; _label="$2"; shift 2
	_out=`run_df "$@"`; _rc=$?
	if [ -z "$_out" ]; then
		# No rows at all, for one of three reasons.
		# Every mounted filesystem is left out by the XYMONCLIENT_FS_* settings:
		# a choice, not a failure. The disk report says so, in the words of the
		# AIX client; the inode report stays empty, as an inode report with
		# nothing to count does. With DF_LOCAL_ONLY=yes the hard-blocking remote
		# types count as left out too: df -l does not report them.
		# Otherwise a non-zero exit is a collection failure - surface a one-line
		# marker (no df header) so the server goes yellow, not green.
		# Otherwise nothing matched this report (e.g. an inode report whose
		# filesystems are all zfs, which are excluded): emit nothing, so the
		# server's empty-report path stays green rather than rejecting a stray
		# header-only row as "columns not found".
		_fex=`fs_excl_opt | sed -e 's/^-tno//' -e 's/,/ /g'`
		[ "$DFLOCALONLY" = yes ] && _fex="$_fex $XYMONCLIENT_FS_REMOTE_HARDBLOCK_TYPES"
		if fs_all_excluded "$_fex"; then
			if [ "$_kind" = disk ]; then
				echo "$_label report collection failed: every filesystem excluded by the XYMONCLIENT_FS_* settings"
			fi
		elif [ "$_rc" -ne 0 ]; then
			echo "$FS_SELF: df $_kind collection failed (status $_rc) with no output; reporting data as unavailable" >&2
			echo "$_label report collection failed: df exited $_rc with no output"
		fi
		return 1
	fi
	# A non-zero df that still prints mounts flows through unchanged.
	return 0
}
