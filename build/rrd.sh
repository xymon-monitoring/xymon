	echo "Checking for RRDtool ..."

	RRDDEF=""
	RRDINC=""
	RRDLIB=""
	PNGLIB=""
	ZLIB=""
	for DIR in /opt/rrdtool* /usr/local/rrdtool* /usr/local /usr/pkg /opt/csw /opt/sfw /usr/sfw
	do
		if test -f $DIR/include/rrd.h
		then
			RRDINC=$DIR/include
		fi

		if test -f $DIR/lib/librrd.so
		then
			RRDLIB=$DIR/lib
		fi
		if test -f $DIR/lib/librrd.a
		then
			RRDLIB=$DIR/lib
		fi
		if test -f $DIR/lib64/librrd.so
		then
			RRDLIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/librrd.a
		then
			RRDLIB=$DIR/lib64
		fi

		if test -f $DIR/lib/libpng.so
		then
			PNGLIB="-L$DIR/lib -lpng"
		fi
		if test -f $DIR/lib/libpng.a
		then
			PNGLIB="-L$DIR/lib -lpng"
		fi
		if test -f $DIR/lib64/libpng.so
		then
			PNGLIB="-L$DIR/lib64 -lpng"
		fi
		if test -f $DIR/lib64/libpng.a
		then
			PNGLIB="-L$DIR/lib64 -lpng"
		fi

		if test -f $DIR/lib/libz.so
		then
			ZLIB="-L$DIR/lib -lz"
		fi
		if test -f $DIR/lib/libz.a
		then
			ZLIB="-L$DIR/lib -lz"
		fi
		if test -f $DIR/lib64/libz.so
		then
			ZLIB="-L$DIR/lib64 -lz"
		fi
		if test -f $DIR/lib64/libz.a
		then
			ZLIB="-L$DIR/lib64 -lz"
		fi
	done

	if test "$USERRRDINC" != ""; then
		RRDINC="$USERRRDINC"
	fi
	if test "$USERRRDLIB" != ""; then
		RRDLIB="$USERRRDLIB"
	fi

	# Probe + compile/link validation
	RRDOK="YES"
	OS=`uname -s | sed -e's@/@_@g'`
	if test "$RRDINC" != ""; then INCOPT="-I$RRDINC"; fi
	if test "$RRDLIB" != ""; then LIBOPT="-L$RRDLIB"; fi

	# --- Helpers ---
	mktemp_xymon() {
		prefix="$1"
		f=
		n=0
		if command -v mktemp >/dev/null 2>&1; then
			f=`mktemp "${TMPDIR:-/tmp}/${prefix}.XXXXXX" 2>/dev/null` && { echo "$f"; return 0; }
		fi
		while test $n -lt 50; do
			f="${TMPDIR:-/tmp}/${prefix}.$$.$n"
			(umask 077; : > "$f") 2>/dev/null && { echo "$f"; return 0; }
			n=`expr $n + 1`
		done
		return 1
	}

	# Probe whether rrd_update() expects const char ** (newer) or char ** (legacy).
	detect_rrd_const_args() {
		CONSTTESTSRC=`mktemp_xymon "xymon-rrd-const"` || return 2
		CONSTTESTOBJ=`mktemp_xymon "xymon-rrd-const-obj"` || { rm -f "$CONSTTESTSRC"; return 2; }
		STRICT_CFLAGS=""
		FLAGTESTOBJ=`mktemp_xymon "xymon-rrd-flag-obj"` || { rm -f "$CONSTTESTSRC" "$CONSTTESTOBJ"; return 2; }
		if ${CC:-cc} -Werror=incompatible-pointer-types -x c -c -o "$FLAGTESTOBJ" - >/dev/null 2>&1 <<EOF
int main(void) { return 0; }
EOF
		then
			STRICT_CFLAGS="-Werror=incompatible-pointer-types"
		fi
		rm -f "$FLAGTESTOBJ"

		cat > "$CONSTTESTSRC" <<EOF
#include <stddef.h>
#include <rrd.h>
int main(void)
{
	const char *args[] = { "rrdupdate", "dummy.rrd", NULL };
	return rrd_update(2, args);
}
EOF
		if ${CC:-cc} ${INCOPT} ${STRICT_CFLAGS} -x c -c "$CONSTTESTSRC" -o "$CONSTTESTOBJ" >/dev/null 2>&1; then
			rm -f "$CONSTTESTSRC" "$CONSTTESTOBJ"
			echo "const"
			return 0
		fi

		rm -f "$CONSTTESTSRC" "$CONSTTESTOBJ"
		echo "mutable"
		return 0
	}

	try_rrd_compile() {
		OS=$OS RRDDEF="$RRDDEF" RRDINC="$INCOPT" $MAKE -f Makefile.test-rrd test-compile
	}

	try_rrd_link() {
		OS=$OS RRDLIB="$LIBOPT" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
	}

	try_rrd_compile_with_legacy_fallback() {
		FIRSTCOMPILELOG=`mktemp_xymon "xymon-rrd-first-compile.log"` || FIRSTCOMPILELOG="/tmp/xymon-rrd-first-compile.log.$$"

		# test-rrd.c exercises update/create/fetch/graph signatures through rrd_compat.h.
		if try_rrd_compile >/dev/null 2>"$FIRSTCOMPILELOG"; then
			rm -f "$FIRSTCOMPILELOG"
			return 0
		fi

		echo "RRD: initial compile probe failed; first diagnostics:"
		head -n 20 "$FIRSTCOMPILELOG"
		echo "RRD: full diagnostics saved at: $FIRSTCOMPILELOG"
		# Initial compile failed; retry with the legacy RRD graph ABI macro.
		echo "RRD: retrying compile probe with RRDTOOL12 compatibility"
		RRDDEF="$RRDDEF -DRRDTOOL12"
		OS=$OS $MAKE -f Makefile.test-rrd clean
		try_rrd_compile
	}

	try_rrd_link_with_fallbacks() {
		try_rrd_link && return 0

		# Could be that we need -lz for RRD
		PNGLIB="$PNGLIB $ZLIB"
		try_rrd_link && return 0

		# Could be that we need -lm for RRD
		PNGLIB="$PNGLIB -lm"
		try_rrd_link && return 0

		# Could be that we need -L/usr/X11R6/lib (OpenBSD)
		LIBOPT="$LIBOPT -L/usr/X11R6/lib"
		RRDLIB="$RRDLIB -L/usr/X11R6/lib"
		try_rrd_link
	}

	# --- Phase 1: ABI detection ---
	# Detect whether RRDtool APIs use const argv pointers.
	# Newer headers expect const char **, older expect char **.
	RRD_CONST_ARGS_DETECTED=`detect_rrd_const_args`
	RRD_CONST_PROBE_STATUS=$?
	if test "$RRD_CONST_PROBE_STATUS" -ne 0; then
		echo "RRD: detect ABI -> error (status=$RRD_CONST_PROBE_STATUS)"
		RRDOK="NO"
		RRD_CONST_ARGS_DETECTED=mutable
	fi
	if test "$RRD_CONST_ARGS_DETECTED" = "const"; then
		echo "RRD: detect ABI -> const argv pointers"
		RRDDEF="$RRDDEF -DRRD_CONST_ARGS=1"
	else
		echo "RRD: detect ABI -> mutable argv pointers"
		RRDDEF="$RRDDEF -DRRD_CONST_ARGS=0"
	fi

	# --- Phase 2: compile/link probes ---
	cd build
	OS=$OS $MAKE -f Makefile.test-rrd clean
	if try_rrd_compile_with_legacy_fallback; then
		echo "RRD: compile probe -> ok"
	else
		echo "RRD: compile probe -> failed"
		RRDOK="NO"
	fi
	echo "RRD: selected compatibility flags -> $RRDDEF"

	if try_rrd_link_with_fallbacks; then
		echo "RRD: link probe -> ok"
		if test "$PNGLIB" != ""; then
			echo "RRD: link probe required extra libraries: $PNGLIB"
		fi
	else
		echo "RRD: link probe -> failed"
		RRDOK="NO"
	fi
	OS=$OS $MAKE -f Makefile.test-rrd clean
	cd ..

	if test "$RRDOK" = "NO"; then
		echo "RRDtool include files or libraries were not found."
		echo "These are required for trend graph support in Xymon, but Xymon can"
		echo "be built without them (for example, for a network-probe-only installation)."
		echo ""
		echo "RRDtool can be found at http://oss.oetiker.ch/rrdtool/"
		echo "If you have RRDtool installed, use the \"--rrdinclude DIR\" and \"--rrdlib DIR\""
		echo "options to configure to specify where they are."
		echo ""
		echo "Continuing with trend graph support disabled."
		sleep 3
	fi
