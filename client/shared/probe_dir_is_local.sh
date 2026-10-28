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
