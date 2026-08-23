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
