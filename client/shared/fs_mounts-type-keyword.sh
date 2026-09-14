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
