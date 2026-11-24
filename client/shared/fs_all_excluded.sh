# fs_all_excluded "TYPE ...": true when the mount list could be read, is not
# empty, and every mount in it has one of the given types. This is how an
# empty report is told from a df that failed: with every type excluded, df has
# nothing to report, and says so by exiting 1 with no output (Linux, OpenBSD)
# or by exiting 0 with no output (FreeBSD, NetBSD) - the first reads as a
# failed collection, the second as nothing at all. Read from fs_mounts, never
# from a second df without the exclusion: that one would stat the very
# filesystems that were left out, a dead remote mount among them.
# A list that cannot be read, or a type this cannot place, answers false, and
# the report stays what it was.
fs_all_excluded() {
	_fae=$(fs_mounts) || return 1
	[ -n "$_fae" ] || return 1
	printf '%s\n' "$_fae" | awk -F'\t' -v excl="$1" '
		BEGIN { n = split(excl, a, /[ \t]+/); for (i = 1; i <= n; i++) x[a[i]] = 1 }
		!($1 in x) { kept = 1 }
		END { exit kept ? 1 : 0 }'
}
