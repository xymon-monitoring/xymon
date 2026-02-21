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
		TESTOBJ=`mktemp_xymon "xymon-rrd-abi-obj"` || return 2
		CONSTOK=0
		MUTABLEOK=0
		RRD_PROBE_CFLAGS=""

		if ${CC:-cc} -Werror=incompatible-pointer-types -x c -c -o "$TESTOBJ" - >/dev/null 2>&1 <<EOF
int main(void) { return 0; }
EOF
		then
			RRD_PROBE_CFLAGS="-Werror=incompatible-pointer-types"
		elif ${CC:-cc} -Werror -x c -c -o "$TESTOBJ" - >/dev/null 2>&1 <<EOF
int main(void) { return 0; }
EOF
		then
			RRD_PROBE_CFLAGS="-Werror"
		fi

		if ${CC:-cc} ${INCOPT} ${RRD_PROBE_CFLAGS} -x c -c -o "$TESTOBJ" - >/dev/null 2>&1 <<EOF
#include <stddef.h>
#include <rrd.h>
int main(void)
{
	const char *args[] = { "rrdupdate", "dummy.rrd", NULL };
	return rrd_update(2, args);
}
EOF
		then
			CONSTOK=1
		fi
		if ${CC:-cc} ${INCOPT} ${RRD_PROBE_CFLAGS} -x c -c -o "$TESTOBJ" - >/dev/null 2>&1 <<EOF
#include <stddef.h>
#include <rrd.h>
int main(void)
{
	char *args[] = { "rrdupdate", "dummy.rrd", NULL };
	return rrd_update(2, args);
}
EOF
		then
			MUTABLEOK=1
		fi

		rm -f "$TESTOBJ"
		case "$CONSTOK:$MUTABLEOK" in
			1:0)
				echo "const"
				return 0
				;;
			0:1)
				echo "mutable"
				return 0
				;;
			1:1)
				echo "RRD: detect ABI is ambiguous (both const and mutable probes compiled)." >&2
				echo "RRD: set USERRRDCONSTARGS=0 or USERRRDCONSTARGS=1 to override." >&2
				return 3
				;;
			*)
				echo "RRD: detect ABI failed (neither const nor mutable probe compiled)." >&2
				echo "RRD: set USERRRDCONSTARGS=0 or USERRRDCONSTARGS=1 to override." >&2
				return 4
				;;
		esac
	}

	try_rrd_compile() {
		OS=$OS RRDDEF="$RRDDEF" RRDINC="$INCOPT" $MAKE -f Makefile.test-rrd test-compile
	}

	try_rrd_link() {
		OS=$OS RRDLIB="$LIBOPT" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
	}

	try_rrd_compile_probe() {
		# test-rrd.c exercises update/create/fetch/graph signatures through rrd_compat.h.
		try_rrd_compile >/dev/null 2>&1
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
	RRD_CONST_ARGS_DETECTED=""
	RRD_CONST_PROBE_STATUS=0
	case "$USERRRDCONSTARGS" in
		1)
			RRD_CONST_ARGS_DETECTED="const"
			echo "RRD: ABI override -> const argv pointers (USERRRDCONSTARGS=$USERRRDCONSTARGS)"
			;;
		0)
			RRD_CONST_ARGS_DETECTED="mutable"
			echo "RRD: ABI override -> mutable argv pointers (USERRRDCONSTARGS=$USERRRDCONSTARGS)"
			;;
		""|auto)
			RRD_CONST_ARGS_DETECTED=`detect_rrd_const_args`
			RRD_CONST_PROBE_STATUS=$?
			;;
		*)
			echo "RRD: invalid USERRRDCONSTARGS value '$USERRRDCONSTARGS' (expected 0, 1, or auto)"
			RRD_CONST_PROBE_STATUS=9
			;;
	esac
	if test "$RRD_CONST_PROBE_STATUS" -ne 0; then
		echo "RRD: detect ABI -> failed (status=$RRD_CONST_PROBE_STATUS)"
		RRDOK="NO"
	fi
	if test "$RRD_CONST_ARGS_DETECTED" = "const"; then
		echo "RRD: detect ABI -> const argv pointers"
		RRDDEF="$RRDDEF -DRRD_CONST_ARGS=1"
	elif test "$RRD_CONST_ARGS_DETECTED" = "mutable"; then
		echo "RRD: detect ABI -> mutable argv pointers"
		RRDDEF="$RRDDEF -DRRD_CONST_ARGS=0"
	else
		echo "RRD: detect ABI -> unresolved"
		RRDOK="NO"
	fi

	# --- Phase 2: compile/link probes ---
	cd build
	OS=$OS $MAKE -f Makefile.test-rrd clean
	if try_rrd_compile_probe; then
		echo "Compiling with RRDtool works OK"
	else
		echo "ERROR: Cannot compile with RRDtool."
		RRDOK="NO"
	fi
	echo "RRD: selected compatibility flags -> $RRDDEF"

	if try_rrd_link_with_fallbacks; then
		echo "Linking with RRDtool works OK"
		if test "$PNGLIB" != ""; then
			echo "Linking RRD needs extra library: $PNGLIB"
		fi
	else
		echo "ERROR: Linking with RRDtool fails"
		RRDOK="NO"
	fi
	OS=$OS $MAKE -f Makefile.test-rrd clean
	cd ..

	if test "$RRDOK" = "NO"; then
		echo "RRDtool include- or library-files not found."
		echo "These are REQUIRED for trend-graph support in Xymon, but Xymon can"
		echo "be built without them (e.g. for a network-probe only installation."
		echo ""
		echo "RRDtool can be found at http://oss.oetiker.ch/rrdtool/"
		echo "If you have RRDtool installed, use the \"--rrdinclude DIR\" and \"--rrdlib DIR\""
		echo "options to configure to specify where they are."
		echo ""
		echo "Continuing with all trend-graph support DISABLED"
		sleep 3
	fi
