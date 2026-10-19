# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/strncat-bounds.awk
#
# Reports every strncat() whose size argument does not subtract one for the
# terminator. Reads one C file, writes "path:line<TAB>bound" per finding.
#
# strncat(dst, src, n) appends up to n characters and then a NUL, so a bound of
# the destination's whole size writes that NUL one byte past the end. The form
# that fits is sizeof(dst) - strlen(dst) - 1.
#
# The third argument is taken by matching parentheses, so a call split over
# several lines is read as one -- that is how the newest of them is written.

{ txt = txt $0 "\n" }

END {
	n = length(txt)
	pos = 1
	while (pos <= n) {
		at = index(substr(txt, pos), "strncat")
		if (at == 0) break
		start = pos + at - 1
		pos = start + 7

		# a call, not a longer identifier ending in strncat
		before = (start > 1) ? substr(txt, start - 1, 1) : " "
		if (before ~ /[A-Za-z0-9_]/) continue

		# skip spaces, then require the open parenthesis
		i = start + 7
		while (i <= n && substr(txt, i, 1) == " ") i++
		if (substr(txt, i, 1) != "(") continue

		# walk the arguments, splitting on top-level commas
		depth = 1; i++; argno = 1; third = ""
		while (i <= n && depth > 0) {
			c = substr(txt, i, 1)
			if (c == "(") depth++
			else if (c == ")") { depth--; if (depth == 0) break }
			else if (c == "," && depth == 1) { argno++; i++; continue }
			if (argno == 3) third = third c
			i++
		}
		if (argno < 3) continue

		# the bound, with whitespace squeezed out
		gsub(/[ \t\n]/, "", third)
		if (index(third, "-1") > 0) continue

		line = 1
		head = substr(txt, 1, start)
		for (k = 1; k <= length(head); k++)
			if (substr(head, k, 1) == "\n") line++

		printf "%s:%d\t%s\n", FILE, line, third
	}
}
