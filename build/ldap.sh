	echo "Checking for LDAP ..."

	LDAPINC=""
	LDAPLIB=""
	for DIR in /opt/openldap* /opt/ldap* /usr/local/openldap* /usr/local/ldap* /usr/local /usr /usr/pkg /opt/csw /opt/sfw
	do
		if test -f $DIR/include/ldap.h
		then
			LDAPINC=$DIR/include
		fi

		if test -f $DIR/lib/libldap.so
		then
			LDAPLIB=$DIR/lib
		fi
		if test -f $DIR/lib/libldap.a
		then
			LDAPLIB=$DIR/lib
		fi
		if test -f $DIR/lib64/libldap.so
		then
			LDAPLIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/libldap.a
		then
			LDAPLIB=$DIR/lib64
		fi
	done

	if test "$USERLDAPINC" != ""; then
		LDAPINC="$USERLDAPINC"
	fi
	if test "$USERLDAPLIB" != ""; then
		LDAPLIB="$USERLDAPLIB"
	fi

	#
	# Some systems require liblber also
	#
	if test -f $LDAPLIB/liblber.a
	then
		LDAPLBER=-llber
	fi
	if test -f $LDAPLIB/liblber.so
	then
		LDAPLBER=-llber
	fi

	if test -z "$LDAPINC" -o -z "$LDAPLIB"; then
		echo "(Open)LDAP include- or library-files not found."
		echo "If you want to perform detailed LDAP tests (queries), you need to"
		echo "install LDAP an LDAP client library that Xymon can use."
		echo "OpenLDAP can be found at http://www.openldap.org/"
		echo ""
		echo "If you have OpenLDAP installed, use the \"--ldapinclude DIR\" and \"--ldaplib DIR\""
		echo "options to configure to specify where they are."
		echo ""
	else
		cd build
		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-ldap clean
		OS=`uname -s | tr '[/]' '[_]'` LDAPINC="-I$LDAPINC" $MAKE -f Makefile.test-ldap test-compile
		if [ $? -eq 0 ]; then
			echo "Found LDAP include files in $LDAPINC"
		else
			echo "WARNING: LDAP include files found in $LDAPINC, but compile fails."
			LDAPINC=""
			LDAPLIB=""
		fi

		OS=`uname -s | tr '[/]' '[_]'` LDAPLIB="-L$LDAPLIB" LDAPLBER="$LDAPLBER" $MAKE -f Makefile.test-ldap test-link
		if [ $? -eq 0 ]; then
			echo "Found LDAP libraries in $LDAPLIB"
			LDAPVENDOR=`./test-ldap vendor`
			LDAPVERSION=`./test-ldap version`
			LDAPCOMPILEFLAGS=`./test-ldap flags`
			# echo "LDAP vendor is $LDAPVENDOR, version $LDAPVERSION"
		else
			echo "WARNING: LDAP library files found in $LDAPLIB, but link fails."
			LDAPINC=""
			LDAPLIB=""
		fi


		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-ldap clean
		cd ..
	fi

	if test -z "$LDAPINC" -o -z "$LDAPLIB"; then
		sleep 3
		echo "Continuing with LDAP support disabled."
	fi

