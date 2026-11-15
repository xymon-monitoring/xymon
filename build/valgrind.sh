#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build/valgrind.sh -- run one Xymon program under Valgrind's Memcheck, and
# say whether it leaked memory or misused it.
#
#   build/valgrind.sh PROGRAM [ARGUMENT...]
#
# PROGRAM runs with Memcheck's full leak check. Valgrind's report goes to a
# file -- VALGRIND_LOG if set, else a new file in TMPDIR -- and its leak and
# error summary, and the file's name, are printed on stderr. Exit status:
#
#   PROGRAM's own  Memcheck found nothing to report
#   99             a memory error (invalid read, write or free, use of an
#                  uninitialised value), or a block definitely or indirectly
#                  lost. "Possibly lost" and "still reachable" are in the
#                  report but do not count: they are often no leak at all --
#                  a pointer into the middle of a block, a cache kept to exit.
#   77             valgrind is not installed (a test reads it as a skip)
#   2              no PROGRAM given
#
# Valgrind is a package of its own: apt-get install valgrind (Debian,
# Ubuntu), dnf install valgrind (RHEL and its rebuilds, Fedora), pkg install
# valgrind (FreeBSD). It does not run on current macOS, Apple Silicon
# included; build with -fsanitize=address there instead.
#
# A PROGRAM that exits 99 or 77 itself cannot be told apart from those; none
# of Xymon's does.
#
# Valgrind reads more options from VALGRIND_OPTS, for instance
# VALGRIND_OPTS="--track-origins=yes --suppressions=my.supp". A PROGRAM that
# forks without exec writes its children's reports to the same file; give
# VALGRIND_LOG a %p (VALGRIND_LOG=/tmp/vg.%p) for one file per process.
#
# Stack traces name functions and lines when PROGRAM has debugging symbols:
# a build from this tree has them (-g), and an installed package has them
# once its debuginfo is installed (dnf debuginfo-install xymon).
#
# A server program needs its configuration, as under xymoncmd(1):
#
#   XYMONHOME=/path/to/scratch build/valgrind.sh xymonnet/xymonnet \
#           --no-update --noping --dns=ip

[ $# -ge 1 ] || { echo "usage: build/valgrind.sh PROGRAM [ARGUMENT...]" >&2; exit 2; }
command -v valgrind >/dev/null 2>&1 || {
	echo "build/valgrind.sh: valgrind is not installed (this script's header says how to install it)" >&2
	exit 77
}

log=${VALGRIND_LOG:-$(mktemp "${TMPDIR:-/tmp}/valgrind.XXXXXX")} || exit 1

rc=0
valgrind --tool=memcheck --leak-check=full \
	--show-leak-kinds=definite,indirect \
	--errors-for-leak-kinds=definite,indirect \
	--error-exitcode=99 \
	--log-file="$log" "$@" || rc=$?

# With a %p in its name the log is one file per process: summarise them all.
case $log in
	*%p*) logs=$(echo "$log" | sed 's/%p/*/g') ;;
	*) logs=$log ;;
esac
# shellcheck disable=SC2086  # $logs is a glob on purpose
grep -h -E 'definitely lost:|indirectly lost:|possibly lost:|ERROR SUMMARY:' $logs \
	| sed 's/^==[0-9]*== *//' >&2
echo "build/valgrind.sh: full report in $log" >&2
exit $rc
