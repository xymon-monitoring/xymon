#!/bin/sh
#
# build/subst.sh NAME ... -- copy stdin to stdout, replacing each @NAME@ with the
# value of the environment variable NAME, literally: no shell, sed or regular
# expression metacharacter is special, so a value with &, !, a backslash, a
# space or a slash is copied through verbatim. The Makefiles that generate a
# config file from its .DIST template set the values in the environment and pass
# the names, so no value reaches sed, a shell word split, or awk's -v / operand
# escaping. A @NAME@ not named is left as it stands; the output is not rescanned.
#
exec awk -v names=" $* " '
BEGIN { for (i = split(names, a, " "); i > 0; i--) want[a[i]] = 1 }
{
	line = $0
	out = ""
	while ((p = index(line, "@")) > 0) {
		out = out substr(line, 1, p - 1)
		line = substr(line, p + 1)
		q = index(line, "@")
		if (q == 0) { out = out "@"; break }
		name = substr(line, 1, q - 1)
		if (name in want) {
			out = out ENVIRON[name]
			line = substr(line, q + 1)
		}
		else out = out "@"	# not ours: its second @ may open one
	}
	print out line
}
'
