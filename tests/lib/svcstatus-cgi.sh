# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/lib/svcstatus-cgi.sh -- shared scaffolding for the svcstatus.cgi
# tests (tests/web/svcstatus-*.sh): build the real CGI from this tree and
# stand up a minimal XYMONHOME with a configured host "realhost". Sourced
# after tests/lib/assert.sh (needs $ROOT set and require_c_buildenv already
# called); not executable, so the runner never discovers it as a test.
#
# svcstatus_setup [--no-daemon]
#     sets $work, writes $work/etc/{xymonserver.cfg,hosts.cfg}, scrapes the
#     link libraries. By default it probes for a connection-refusing local
#     port and points XYMONDPORT at it, so a live request fails fast with
#     connection-refused. --no-daemon skips that probe (the caller stands up
#     its own responder and appends XYMONDPORT itself), avoiding both the
#     wasted probe and a duplicate XYMONDPORT line.
# svcstatus_build [cflags..]
#     builds $work/svcstatus, reusing a tree+flags-keyed cached binary when
#     the sources and archives are older than it (the four split tests would
#     otherwise recompile the same 3-file CGI from scratch each run).
#     Returns 1 on a compile/link failure ($work/cc.log kept) so a caller
#     can fall back (e.g. from an ASAN build to a plain one).
# render QS / render_live QS
#     run the CGI (historical / live mode) with QUERY_STRING=QS; set OUT and
#     RC, fail on a crash exit code. Extra svcstatus args go after QS; extra
#     environment is passed via $RENDER_ENV (e.g. "REMOTE_USER=alice").

svcstatus_setup() {
	local with_daemon=1
	[ "${1:-}" = "--no-daemon" ] && with_daemon=0

	[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"

	# svcstatus.cgi links against SSLLIBS + NETLIBS + LIBRTDEF + PCRELIBS;
	# scrape all four from the Makefile (NETLIBS is "-lsocket -lnsl" on
	# Solaris, LIBRTDEF "-lrt" on older glibc, PCRELIBS may carry a -L path
	# from configure's --pcrelib). Omitting them fails the link on a tree
	# that builds fine. Keep the pkg-config/-lpcre2-8 fallback for a
	# Makefile without PCRELIBS.
	pcre_libs=${PCRELIBS:-$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")}
	if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
		pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
	fi
	[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

	work=$(mktempdir)

	build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a

	mkdir -p "$work/etc"
	cp "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"

	# Appended lines override the DIST defaults (loadenv() lets a later
	# putenv() win). Override HOSTSCFG directly, not XYMONHOME -- HOSTSCFG
	# is expanded earlier in the file. XYMONVAR/XYMONHISTLOGS must be
	# overridden here too: loadenv() putenv()s over the real environment,
	# so the values render()/render_live() pass as env vars would lose to
	# the DIST placeholders ("@XYMONVAR@/histlogs").
	{
		echo "HOSTSCFG=\"$work/etc/hosts.cfg\""
		echo 'XYMSERVERS=""'
		echo 'XYMSRV="127.0.0.1"'
		echo "XYMONVAR=\"$work/var\""
		echo "XYMONHISTLOGS=\"$work/var/histlogs\""
	} >>"$work/etc/xymonserver.cfg"

	if [ "$with_daemon" = 1 ]; then
		# The daemon address must point at a dead local port so a live
		# CLIENT request fails with connection-refused instead of
		# depending on whether a real xymond happens to be listening.
		# Probe candidates and take the first that refuses a connect
		# (hardcoding one port breaks on a machine where something
		# listens there); the connect probe runs in a subshell so a
		# successful connect is closed again immediately.
		local deadport=
		for cand in 59999 56313 51724 49517 47311; do
			if ! (exec 3<>"/dev/tcp/127.0.0.1/$cand") 2>/dev/null; then deadport=$cand; break; fi
		done
		[ -n "$deadport" ] || skip "no connection-refusing localhost port found for the dead-xymond endpoint"
		echo "XYMONDPORT=\"$deadport\"" >>"$work/etc/xymonserver.cfg"
	fi

	# A configured host, so loadhostdata() succeeds on the historical-status
	# path (otherwise every request there is refused as "No such host" and
	# the guard under test is never exercised).
	printf '127.0.0.1 realhost # \n' >"$work/etc/hosts.cfg"
}

# svcstatus.cgi is svcstatus.o + svcstatus-info.o + svcstatus-trends.o
# (web/Makefile SVCSTATUSOBJS); build them here so the test does not depend
# on the tree having been built already.
# The archives are listed twice rather than wrapped in --start-group:
# --start-group is GNU ld only, and the Darwin linker rejects it.
#
# The link is cached in a tree+flags-keyed location and reused when none of
# the inputs is newer than it: the four split svcstatus tests build the
# identical binary, so the first pays the compile and the rest copy it. The
# freshness set covers the three .c files, the headers svcstatus.c includes
# (svcstatus-info.h, svcstatus-trends.h, version.h), and the two lib archives
# -- build_xymon_libs relinks an archive when a lib source or header (cgi.c,
# libxymon.h, ...) changes, bumping its mtime. So any code change in the tree
# is newer than the cache and forces a rebuild; the cache never hides one.
svcstatus_build() {
	local flagkey bin harness_cflags harness_ldflags ccpath
	harness_cflags=$(xymon_cflags "$ROOT")
	harness_ldflags=$(xymon_ldflags "$ROOT")
	# The compiler decides the binary as much as the flags do. Left out of the
	# key, a cache filled by one compiler is handed straight to a run that
	# asked for another, and the test passes without that compiler ever being
	# invoked -- a gcc-built binary satisfying a run meant to exercise clang.
	# The resolved path as well as the name: "cc" is a different compiler on
	# different hosts, and PATH can put another one under the same name.
	ccpath=$(command -v "${CC%% *}" 2>/dev/null) || ccpath=$CC
	# The key has to cover everything that changes the binary, not just the
	# arguments this was called with: the configured compile and link flags
	# decide the result too. Keyed on the arguments alone, changing SSLLIBS,
	# a library search path or the rpath reuses a stale cached binary, and
	# the test passes without ever exercising the configuration it claims to.
	# Hashed rather than spelled out because the flags carry absolute paths.
	flagkey=$(printf '%s' "$CC $ccpath $* $harness_cflags $harness_ldflags $pcre_libs" | cksum | tr -cd '0-9')
	bin="${TMPDIR:-/tmp}/xymon-svcstatus-cache.$(id -u)/$(printf '%s' "$ROOT" | tr -c 'A-Za-z0-9' '_').${flagkey:-plain}"
	mkdir -p "$(dirname "$bin")"

	if [ -x "$bin" ] && [ -z "$(find \
			"$ROOT/web/svcstatus.c" "$ROOT/web/svcstatus-info.c" "$ROOT/web/svcstatus-trends.c" \
			"$ROOT/web/svcstatus-info.h" "$ROOT/web/svcstatus-trends.h" "$ROOT/include/version.h" \
			"$ROOT/lib/libxymon.a" "$ROOT/lib/libxymoncomm.a" \
			"$ccpath" -newer "$bin" 2>/dev/null)" ]; then
		cp "$bin" "$work/svcstatus"
		return 0
	fi

	"$CC" "$@" $harness_cflags -iquote "$ROOT/web" -o "$work/svcstatus" \
		"$ROOT/web/svcstatus.c" "$ROOT/web/svcstatus-info.c" "$ROOT/web/svcstatus-trends.c" \
		"$ROOT/lib/libxymon.a" "$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymon.a" \
		$pcre_libs $harness_ldflags 2>"$work/cc.log" || return 1
	cp "$work/svcstatus" "$bin"
}

# svcstatus_run MODE QS [extra svcstatus args...] -- run the CGI with
# QUERY_STRING=QS; set OUT and RC. MODE is "--historical" or "" (live).
# $RENDER_ENV supplies extra environment tokens (e.g. "REMOTE_USER=alice").
svcstatus_run() {
	local mode=$1 qs=$2
	shift 2
	set +e
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING="$qs" \
	      XYMONHOME="$work" XYMONVAR="$work/var" CLIENTLOGS="$work/var/hostdata" \
	      XYMONHISTLOGS="$work/var/histlogs" \
	      ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
	      ${RENDER_ENV:-} \
		"$work/svcstatus" $mode --env="$work/etc/xymonserver.cfg" "$@" 2>/dev/null)
	RC=$?
	set -e
	# 0 = page served, 1 = errormsg() refusal. Anything else (137, 139, ...)
	# is a crash, which would otherwise look exactly like "no canary found".
	[ "$RC" -le 1 ] || fail "svcstatus exited $RC (crash?) on QUERY_STRING=$qs"
}

render()      { svcstatus_run --historical "$@"; }
render_live() { svcstatus_run "" "$@"; }
