	echo "Checking for LDAP ..."

	LDAPINC=""
	LDAPLIB=""
	for DIR in /opt/openldap* /opt/ldap* /usr/local/openldap* /usr/local/ldap* /usr/local /usr /usr/pkg
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
	done

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
		echo "install LDAP an LDAP client library that bbgen can use."
		echo "OpenLDAP can be found at http://www.openldap.org/"
		echo "Continuing with LDAP support disabled."
	else
		cd build
		OS=`uname -s` $MAKE -f Makefile.test-ldap clean
		OS=`uname -s` LDAPINC="-I$LDAPINC" $MAKE -f Makefile.test-ldap test-compile
		if [ $? -eq 0 ]; then
			echo "Found LDAP include files in $LDAPINC"
		else
			echo "WARNING: LDAP include files found in $LDAPINC, but compile fails."
		fi

		OS=`uname -s` LDAPLIB="-L$LDAPLIB" LDAPLBER="$LDAPLBER" $MAKE -f Makefile.test-ldap test-link
		if [ $? -eq 0 ]; then
			echo "Found LDAP libraries in $LDAPLIB"
		else
			echo "WARNING: LDAP library files found in $LDAPLIB, but link fails."
		fi

		LDAPVENDOR=`./test-ldap`

		OS=`uname -s` $MAKE -f Makefile.test-ldap clean
		cd ..
	fi


