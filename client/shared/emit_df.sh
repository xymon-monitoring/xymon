# emit_df KIND LABEL FLAG [extra-excludes...]: run_df + failure guard. Leaves
# the rows in $_out and returns 0 to format them; returns 1 when there is
# nothing to format. $FS_SELF names this client in the message on stderr.
emit_df() {
	_kind="$1"; _label="$2"; shift 2
	_out=`run_df "$@"`; _rc=$?
	if [ -z "$_out" ]; then
		# No rows at all. A non-zero exit is a collection failure - surface a
		# one-line marker (no df header) so the server goes yellow, not green.
		# A clean exit means nothing matched (e.g. an inode report whose
		# filesystems are all zfs, which are excluded): emit nothing, so the
		# server's empty-report path stays green rather than rejecting a stray
		# header-only row as "columns not found". FreeBSD's and NetBSD's df
		# exit 0 then; OpenBSD's exits 1 and is reported as a failure above.
		if [ "$_rc" -ne 0 ]; then
			echo "$FS_SELF: df $_kind collection failed (status $_rc) with no output; reporting data as unavailable" >&2
			echo "$_label report collection failed: df exited $_rc with no output"
		fi
		return 1
	fi
	# A non-zero df that still prints mounts flows through unchanged.
	return 0
}
