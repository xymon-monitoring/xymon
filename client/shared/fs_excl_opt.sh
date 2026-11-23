# fs_excl_opt [extra-excludes...]: the df option that leaves filesystem types
# out, "-tno<csv>", or nothing when no type is left to exclude. The types are
# this client's own ($FS_EXCLUDE_DEFAULT) and the report's ("$@", e.g. zfs for
# the inode report), minus XYMONCLIENT_FS_INCLUDE_TYPES, plus
# XYMONCLIENT_FS_EXCLUDE_TYPES.
fs_excl_opt() {
	# noglob: treat a type like "tmp*" as a literal token, not a filename glob.
	case $- in *f*) _restoreglob=no ;; *) _restoreglob=yes; set -f ;; esac
	_l=""
	# Default + per-report excludes, minus any INCLUDE_TYPES.
	for _t in $FS_EXCLUDE_DEFAULT "$@"; do
		for _i in $XYMONCLIENT_FS_INCLUDE_TYPES; do [ "$_t" = "$_i" ] && continue 2; done
		case " $_l " in *" $_t "*) ;; *) _l="$_l $_t" ;; esac
	done
	# EXCLUDE_TYPES last, so a type in both lists stays excluded (EXCLUDE wins).
	for _t in $XYMONCLIENT_FS_EXCLUDE_TYPES; do
		case " $_l " in *" $_t "*) ;; *) _l="$_l $_t" ;; esac
	done
	_csv=`echo $_l | tr ' ' ','`
	[ "$_restoreglob" = yes ] && set +f
	[ -n "$_csv" ] && printf -- '-tno%s' "$_csv"
}
