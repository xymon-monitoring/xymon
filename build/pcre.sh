echo "Checking for PCRE ..."

PCRE_MAJOR="${PCRE_MAJOR:-1}"
if test "$PCRE_MAJOR" != "1" -a "$PCRE_MAJOR" != "2"; then
	echo "ERROR: PCRE_MAJOR must be 1 or 2 (got '$PCRE_MAJOR')"
	exit 1
fi

PCREINC=""
PCRELIB=""
PCRETESTSRC="test-pcre.c"
PCRETESTOBJ="test-pcre.o"
PCRETESTBIN="test-pcre"
PCRELINKLIB="-lpcre"

for DIR in /opt/pcre* /usr/local/pcre* /usr/local /usr/pkg /opt/csw /opt/sfw
do
	if test "$PCRE_MAJOR" = "2"; then
		if test -f "$DIR/include/pcre2.h"
		then
			PCREINC=$DIR/include
		fi
		if test -f "$DIR/include/pcre2/pcre2.h"
		then
			PCREINC=$DIR/include/pcre2
		fi

		if test -f "$DIR/lib/libpcre2-8.so"
		then
			PCRELIB=$DIR/lib
		fi
		if test -f "$DIR/lib/libpcre2-8.a"
		then
			PCRELIB=$DIR/lib
		fi
		if test -f "$DIR/lib64/libpcre2-8.so"
		then
			PCRELIB=$DIR/lib64
		fi
		if test -f "$DIR/lib64/libpcre2-8.a"
		then
			PCRELIB=$DIR/lib64
		fi
	else
		if test -f "$DIR/include/pcre.h"
		then
			PCREINC=$DIR/include
		fi
		if test -f "$DIR/include/pcre/pcre.h"
		then
			PCREINC=$DIR/include/pcre
		fi

		if test -f "$DIR/lib/libpcre.so"
		then
			PCRELIB=$DIR/lib
		fi
		if test -f "$DIR/lib/libpcre.a"
		then
			PCRELIB=$DIR/lib
		fi
		if test -f "$DIR/lib64/libpcre.so"
		then
			PCRELIB=$DIR/lib64
		fi
		if test -f "$DIR/lib64/libpcre.a"
		then
			PCRELIB=$DIR/lib64
		fi
	fi
done

if test "$USERPCREINC" != ""; then
	PCREINC="$USERPCREINC"
fi
if test "$USERPCRELIB" != ""; then
	PCRELIB="$USERPCRELIB"
fi

# Lets see if it can build
PCREOK="YES"
if test "$PCRE_MAJOR" = "2"; then
	PCRETESTSRC="test-pcre2.c"
	PCRETESTOBJ="test-pcre2.o"
	PCRETESTBIN="test-pcre2"
	PCRELINKLIB="-lpcre2-8"
fi

echo "PCRE probe mode: PCRE$PCRE_MAJOR"

cd build
if test "$PCREINC" != ""; then INCOPT="-I$PCREINC"; fi
if test "$PCRELIB" != ""; then LIBOPT="-L$PCRELIB"; fi

OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-pcre clean
OS=`uname -s | sed -e's@/@_@g'` \
	PCREINC="$INCOPT" \
	PCRETESTSRC="$PCRETESTSRC" \
	PCRETESTOBJ="$PCRETESTOBJ" \
	PCRETESTBIN="$PCRETESTBIN" \
	PCRELINKLIB="$PCRELINKLIB" \
	$MAKE -f Makefile.test-pcre test-compile
if test $? -eq 0; then
	echo "Compiling with PCRE library works OK"
else
	echo "ERROR: Cannot compile using PCRE library."
	PCREOK="NO"
fi

OS=`uname -s | sed -e's@/@_@g'` \
	PCRELIB="$LIBOPT" \
	PCRETESTSRC="$PCRETESTSRC" \
	PCRETESTOBJ="$PCRETESTOBJ" \
	PCRETESTBIN="$PCRETESTBIN" \
	PCRELINKLIB="$PCRELINKLIB" \
	$MAKE -f Makefile.test-pcre test-link
if test $? -eq 0; then
	echo "Linking with PCRE library works OK"
else
	echo "ERROR: Cannot link with PCRE library."
	PCREOK="NO"
fi

OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-pcre clean
cd ..

if test "$PCREOK" = "NO"; then
	echo "Missing PCRE include- or library-files. These are REQUIRED for xymond"
	echo "PCRE can be found at http://www.pcre.org/"
	echo "If you have PCRE installed, use the \"--pcreinclude DIR\" and \"--pcrelib DIR\""
	echo "options to configure to specify where they are."
	exit 1
fi
