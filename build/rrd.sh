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
	OS="$(uname -s | sed 's@/@_@g')"

	test -n "$RRDINC" && INCOPT="-I$RRDINC"
	test -n "$RRDLIB" && LIBOPT="-L$RRDLIB"

	detect_rrd_const_args() {
		${CC:-cc} ${INCOPT} -Werror=incompatible-pointer-types \
			-x c -c -o /dev/null - >/dev/null 2>&1 <<EOF
#include <rrd.h>
int main(void) {
	const char *args[] = { "rrdupdate", "dummy.rrd", NULL };
	return rrd_update(2, args);
}
EOF
		test $? -eq 0 && return 1

		${CC:-cc} ${INCOPT} -Werror=incompatible-pointer-types \
			-x c -c -o /dev/null - >/dev/null 2>&1 <<EOF
#include <rrd.h>
int main(void) {
	char *args[] = { "rrdupdate", "dummy.rrd", NULL };
	return rrd_update(2, args);
}
EOF
		test $? -eq 0 && return 0

	return 2
	}

	# --- ABI detection ---
	case "$USERRRDCONSTARGS" in
		1) RRDDEF="$RRDDEF -DRRD_CONST_ARGS=1" ;;
		0) RRDDEF="$RRDDEF -DRRD_CONST_ARGS=0" ;;
		""|auto)
			detect_rrd_const_args || RRDOK="NO"
			case $? in
			1) RRDDEF="$RRDDEF -DRRD_CONST_ARGS=1" ;;
			0) RRDDEF="$RRDDEF -DRRD_CONST_ARGS=0" ;;
			*) RRDOK="NO" ;;
			esac
			;;
		*) RRDOK="NO" ;;
	esac

	# --- Compile / Link ---
	cd build || exit 1
	OS=$OS $MAKE -f Makefile.test-rrd clean

	if OS=$OS RRDDEF="$RRDDEF" RRDINC="$INCOPT" \
		$MAKE -f Makefile.test-rrd test-compile >/dev/null 2>&1
	then
		echo "Compiling with RRDtool works OK"
	else
		echo "ERROR: Cannot compile with RRDtool."
		RRDOK="NO"
	fi

	LINKOK=0
	for EXTRA in "" "$ZLIB" "-lm" "-L/usr/X11R6/lib"
	do
		test -n "$EXTRA" && PNGLIB="$PNGLIB $EXTRA"
		if OS=$OS RRDLIB="$LIBOPT" PNGLIB="$PNGLIB" \
			$MAKE -f Makefile.test-rrd test-link >/dev/null 2>&1
		then
			LINKOK=1
			break
		fi
	done

	if test "$LINKOK" -eq 1; then
		echo "Linking with RRDtool works OK"
		test -n "$PNGLIB" && echo "Linking RRD needs extra library: $PNGLIB"
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
		sleep 1
	fi