	echo "Checking for SQLite3 ..."

	SQLITEINC=""
	SQLITELIB=""
	for DIR in /opt/pcre* /usr/local/pcre* /usr/local /usr/pkg /opt/csw /opt/sfw
	do
		if test -f $DIR/include/sqlite3.h
		then
			SQLITEINC=$DIR/include
		fi
		if test -f $DIR/include/sqlite/sqlite3.h
		then
			SQLITEINC=$DIR/include/sqlite
		fi
		if test -f $DIR/include/sqlite3/sqlite3.h
		then
			SQLITEINC=$DIR/include/sqlite3
		fi

		if test -f $DIR/lib/libsqlite3.so
		then
			SQLITELIB=$DIR/lib
		fi
		if test -f $DIR/lib/libsqlite3.a
		then
			SQLITELIB=$DIR/lib
		fi
		if test -f $DIR/lib64/libsqlite3.so
		then
			SQLITELIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/libsqlite3.a
		then
			SQLITELIB=$DIR/lib64
		fi
	done

	if test "$USERSQLITEINC" != ""; then
		SQLITEINC="$USERSQLITEINC"
	fi
	if test "$USERSQLITELIB" != ""; then
		SQLITELIB="$USERSQLITELIB"
	fi

	# Lets see if it can build
	SQLITEOK="YES"
	cd build
	if test ! -z $SQLITEINC; then INCOPT="-I$SQLITEINC"; fi
	if test ! -z $SQLITELIB; then LIBOPT="-L$SQLITELIB"; fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-sqlite clean
	OS=`uname -s | sed -e's@/@_@g'` SQLITEINC="$INCOPT" $MAKE -f Makefile.test-sqlite test-compile
	if test $? -eq 0; then
		echo "Compiling with SQLITE library works OK"
	else
		echo "ERROR: Cannot compile using SQLITE library."
		SQLITEOK="NO"
	fi

	OS=`uname -s | sed -e's@/@_@g'` SQLITELIB="$LIBOPT" $MAKE -f Makefile.test-sqlite test-link
	if test $? -eq 0; then
		echo "Linking with SQLITE library works OK"
	else
		echo "ERROR: Cannot link with SQLITE library."
		SQLITEOK="NO"
	fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-sqlite clean
	cd ..

	if test "$SQLITEOK" = "NO"; then
		echo "Missing SQLITE include- or library-files. These are REQUIRED for xymond"
		echo "SQLITE can be found at http://www.sqlite.org/"
		echo "If you have SQLITE installed, use the \"--sqliteinclude DIR\" and \"--sqlitelib DIR\""
		echo "options to configure to specify where they are."
		exit 1
	fi


