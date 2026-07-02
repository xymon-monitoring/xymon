#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

# Scope: this harness covers PR #44 only (custom-graph RRD counting, graph
# links, the HTML log path, and the trends page). PR #38 (trends buffer) and
# PR #46 (SKIPLOC) are separate changes with their own tests and are not
# exercised here, so PR #44 can be reviewed and run standalone on main.

make_cmd=${MAKE:-make}
cc=${CC:-cc}

if [ ! -f include/config.h ]; then
	MAKE="$make_cmd" CC="$cc" CFLAGS="${CFLAGS:-}" LDFLAGS="${LDFLAGS:-}" build/genconfig.sh
fi

"$make_cmd" -C lib libxymoncomm.a
"$make_cmd" -C web CFLAGS="-I../include ${CFLAGS:-}" svcstatus-trends.o

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ]; then
	if command -v pkg-config >/dev/null 2>&1; then
		pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
	fi
fi
if [ -z "$pcre_libs" ]; then
	pcre_libs="-lpcre2-8"
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/xymon-pr44-count.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

mkdir -p "$tmpdir/server/etc/graphs.d" \
	"$tmpdir/rrd/testhost" \
	"$tmpdir/rrd/otherhost" \
	"$tmpdir/rrd/limitedhost" \
	"$tmpdir/rrd/mappedhost"

cat >"$tmpdir/server/etc/graphs.cfg" <<'EOF'
[disk]
	FNPATTERN ^olddisk(.*).rrd

[tcp]
	FNPATTERN ^tcp.(.+).rrd
	EXFNPATTERN ^tcp.http.(.+).rrd

[http]
	FNPATTERN ^tcp.http.(.+).rrd

[ncv]
	FNPATTERN ^ncv.(.+).rrd

[devmon]
	FNPATTERN ^devmon.(.+).rrd

[bundle]
	FNPATTERN ^bundle.(.+).rrd

[primary-match]
	FNPATTERN ^primary-match.(.+).rrd

[primary-def]
	DEF:v=primary-def-source.rrd:value:AVERAGE

[la]
	DEF:avg=la.rrd:la:AVERAGE

[vmstat1]
	DEF:cpu_idl=vmstat.rrd:cpu_idl:AVERAGE

[plain]
	TITLE Plain fallback graph

[many]
	FNPATTERN ^many\.(.+)\.rrd

include overrides.cfg
directory graphs.d
EOF

cat >"$tmpdir/server/etc/overrides.cfg" <<'EOF'
[disk]
	FNPATTERN ^disk(.*).rrd

[smart-temp]
	FNPATTERN ^smart-temp\.(.+)\.rrd
EOF

: >"$tmpdir/server/etc/protocols.cfg"

cat >"$tmpdir/server/etc/hosts.cfg" <<'EOF'
127.0.0.1 testhost # NAME:"Test Host"
127.0.0.2 limitedhost # NAME:"Limited Host" TRENDS:smart-temp
127.0.0.3 mappedhost # NAME:"Mapped Host" TRENDS:smart-temp:smart|smart-temp
EOF

cat >"$tmpdir/server/etc/graphs.d/10-extra.cfg" <<'EOF'
[dirgraph]
	FNPATTERN ^dirgraph\.(.+)\.rrd

[commented]
	# FNPATTERN ^commented\.bad\.rrd
	FNPATTERN ^commented\.good\.rrd
EOF

for rrd in \
	disk.root.rrd \
	disk.var.rrd \
	olddisk.stale.rrd \
	smart-temp.cpu.rrd \
	smart-temp.nvme.rrd \
	dirgraph.one.rrd \
	commented.good.rrd \
	commented.bad.rrd \
	tcp.smtp.rrd \
	tcp.ssh.rrd \
	tcp.http.example.rrd \
	ncv.foo.rrd \
	ncv.bar.rrd \
	devmon.temp.rrd \
	devmon.voltage.rrd \
	bundle.smtp.rrd \
	bundle.ssh.rrd \
	bundle.primary-match.rrd \
	bundle.primary-def.rrd \
	primary-match.only.rrd \
	primary-def-source.rrd \
	la.rrd \
	vmstat.rrd \
	plain.rrd \
	orphan.rrd
do
	: >"$tmpdir/rrd/testhost/$rrd"
done

for rrd in \
	disk.root.rrd \
	tcp.smtp.rrd
do
	: >"$tmpdir/rrd/otherhost/$rrd"
done

for host in limitedhost mappedhost
do
	for rrd in \
		smart-temp.cpu.rrd \
		smart-temp.nvme.rrd \
		tcp.smtp.rrd
	do
		: >"$tmpdir/rrd/$host/$rrd"
	done
done

trend_graphs="smart-temp,tcp,tcp.smtp,tcp.http,la"

"$cc" -Iinclude -Ilib -o "$tmpdir/count-rrd-files" \
	tests/count-rrd-files.c lib/libxymoncomm.a $pcre_libs
"$cc" -Iinclude -Ilib -o "$tmpdir/count-rrd-negative" \
	tests/count-rrd-negative.c lib/libxymoncomm.a $pcre_libs
"$cc" -Iinclude -Ilib -o "$tmpdir/graph-link-data" \
	tests/graph-link-data.c lib/libxymoncomm.a $pcre_libs
"$cc" -Iinclude -Ilib -o "$tmpdir/html-log-graphs" \
	tests/html-log-graphs.c lib/libxymoncomm.a $pcre_libs
"$cc" -Iinclude -Ilib -Iweb -o "$tmpdir/trends-page-graphs" \
	tests/trends-page-graphs.c web/svcstatus-trends.o lib/libxymoncomm.a $pcre_libs

XYMONHOME="$tmpdir/server" \
XYMONRRDS="$tmpdir/rrd" \
	"$tmpdir/count-rrd-files"

mkdir -p "$tmpdir/no-graphs-server/etc"
XYMONHOME="$tmpdir/no-graphs-server" \
XYMONRRDS="$tmpdir/rrd" \
	"$tmpdir/count-rrd-negative" missing-graphs 2>"$tmpdir/missing-graphs.err"
grep "Cannot open graphs.cfg" "$tmpdir/missing-graphs.err" >/dev/null

mkdir -p "$tmpdir/bad-server/etc" "$tmpdir/bad-rrd/testhost"
cat >"$tmpdir/bad-server/etc/graphs.cfg" <<'EOF'
[badpattern]
	FNPATTERN [

[goodpattern]
	FNPATTERN ^goodpattern\.rrd
EOF
: >"$tmpdir/bad-rrd/testhost/badpattern.rrd"
: >"$tmpdir/bad-rrd/testhost/goodpattern.rrd"
XYMONHOME="$tmpdir/bad-server" \
XYMONRRDS="$tmpdir/bad-rrd" \
	"$tmpdir/count-rrd-negative" invalid-pattern 2>"$tmpdir/invalid-pattern.err"
grep "Bad FNPATTERN" "$tmpdir/invalid-pattern.err" >/dev/null

XYMONHOME="$tmpdir/server" \
XYMONRRDS="$tmpdir/rrd" \
	"$tmpdir/count-rrd-negative" missing-rrddir 2>"$tmpdir/missing-rrddir.err"
grep "Cannot open RRD directory" "$tmpdir/missing-rrddir.err" >/dev/null

CGIBINURL="/xymon-cgi" \
RRDWIDTH=576 \
RRDHEIGHT=120 \
XYMONSKIN="/xymon/gifs" \
IMAGEFILETYPE="gif" \
	"$tmpdir/graph-link-data"

XYMONHOME="$tmpdir/server" \
XYMONRRDS="$tmpdir/rrd" \
CGIBINURL="/xymon-cgi" \
RRDWIDTH=576 \
RRDHEIGHT=120 \
XYMONSKIN="/xymon/gifs" \
XYMONWEB="/xymon" \
IMAGEFILETYPE="gif" \
GRAPHS="smart-temp" \
GRAPHS_smart="smart-temp" \
INFOCOLUMN="info" \
TRENDSCOLUMN="trends" \
ACKUNTILMSG="until %H:%M" \
	"$tmpdir/html-log-graphs"

XYMONHOME="$tmpdir/server" \
HOSTSCFG="!$tmpdir/server/etc/hosts.cfg" \
XYMONRRDS="$tmpdir/rrd" \
CGIBINURL="/xymon-cgi" \
RRDWIDTH=576 \
RRDHEIGHT=120 \
XYMONSKIN="/xymon/gifs" \
IMAGEFILETYPE="gif" \
GRAPHS="$trend_graphs" \
TEST2RRD="cpu=la,disk,tcp,ncv,devmon,smart=smart-temp" \
	"$tmpdir/trends-page-graphs"
