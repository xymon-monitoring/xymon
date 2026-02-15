echo "Checking for PCRE ..."

# ------------------------------------------------------------
# Auto-detect PCRE version if not forced
# ------------------------------------------------------------
if test -z "$PCRE_MAJOR"; then
        echo "Auto-detecting PCRE version..."

        if command -v pkg-config >/dev/null 2>&1; then
                if pkg-config --exists libpcre2-8 2>/dev/null; then
                        PCRE_MAJOR=2
                        echo "PCRE2 detected via pkg-config"
                elif pkg-config --exists libpcre 2>/dev/null; then
                        PCRE_MAJOR=1
                        echo "PCRE1 detected via pkg-config"
                else
                        PCRE_MAJOR=2
                fi
        else
                PCRE_MAJOR=2
        fi
fi

if test "$PCRE_MAJOR" != "1" -a "$PCRE_MAJOR" != "2"; then
        echo "ERROR: PCRE_MAJOR must be 1 or 2 (got '$PCRE_MAJOR')"
        exit 1
fi

PCREINC=""
PCRELIB=""
PCREDEF=""
PCRETESTSRC="test-pcre.c"
PCRETESTOBJ="test-pcre.o"
PCRETESTBIN="test-pcre"
PCRELINKLIB="-lpcre"

# ------------------------------------------------------------
# Manual directory probing
# ------------------------------------------------------------
for DIR in /opt/pcre* /usr/local/pcre* /usr/local /usr/pkg /opt/csw /opt/sfw
do
        if test "$PCRE_MAJOR" = "2"; then
                if test -f "$DIR/include/pcre2.h"; then PCREINC=$DIR/include; fi
                if test -f "$DIR/include/pcre2/pcre2.h"; then PCREINC=$DIR/include/pcre2; fi

                if test -f "$DIR/lib/libpcre2-8.so" -o -f "$DIR/lib/libpcre2-8.a"; then PCRELIB=$DIR/lib; fi
                if test -f "$DIR/lib64/libpcre2-8.so" -o -f "$DIR/lib64/libpcre2-8.a"; then PCRELIB=$DIR/lib64; fi
        else
                if test -f "$DIR/include/pcre.h"; then PCREINC=$DIR/include; fi
                if test -f "$DIR/include/pcre/pcre.h"; then PCREINC=$DIR/include/pcre; fi

                if test -f "$DIR/lib/libpcre.so" -o -f "$DIR/lib/libpcre.a"; then PCRELIB=$DIR/lib; fi
                if test -f "$DIR/lib64/libpcre.so" -o -f "$DIR/lib64/libpcre.a"; then PCRELIB=$DIR/lib64; fi
        fi
done

if test "$USERPCREINC" != ""; then PCREINC="$USERPCREINC"; fi
if test "$USERPCRELIB" != ""; then PCRELIB="$USERPCRELIB"; fi

# ------------------------------------------------------------
# Setup test parameters
# ------------------------------------------------------------
PCREOK="YES"

if test "$PCRE_MAJOR" = "2"; then
        PCRETESTSRC="test-pcre2.c"
        PCRETESTOBJ="test-pcre2.o"
        PCRETESTBIN="test-pcre2"
        PCRELINKLIB="-lpcre2-8"
        PCREDEF="-DPCRE2"
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

# ------------------------------------------------------------
# Automatic fallback PCRE2 → PCRE1
# ------------------------------------------------------------
if test $? -ne 0 -a "$PCRE_MAJOR" = "2"; then
        echo "PCRE2 test failed, attempting fallback to PCRE1..."

        PCRE_MAJOR=1
        PCREDEF=""
        PCRETESTSRC="test-pcre.c"
        PCRETESTOBJ="test-pcre.o"
        PCRETESTBIN="test-pcre"
        PCRELINKLIB="-lpcre"

        OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-pcre clean
        OS=`uname -s | sed -e's@/@_@g'` \
                PCREINC="$INCOPT" \
                PCRETESTSRC="$PCRETESTSRC" \
                PCRETESTOBJ="$PCRETESTOBJ" \
                PCRETESTBIN="$PCRETESTBIN" \
                PCRELINKLIB="$PCRELINKLIB" \
                $MAKE -f Makefile.test-pcre test-compile
fi

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
        exit 1
fi

echo "Final PCRE mode selected: PCRE$PCRE_MAJOR"
export PCRE_MAJOR
export PCREDEF

