	echo "Checking for LDAP ..."

	LDAPINC=""
	LDAPLIB=""
	for DIR in /opt/openldap* /opt/ldap* /usr/local/openldap* /usr/local/ldap* /usr/local /usr/pkg /opt/csw /opt/sfw
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

	# See if it builds
	LDAPOK="YES"
	if test "$LDAPINC" != ""; then INCOPT="-I$LDAPINC"; fi
	if test "$LDAPLIB" != ""; then LIBOPT="-L$LDAPLIB"; fi
	cd build
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-ldap clean
	OS=`uname -s | tr '[/]' '[_]'` LDAPINC="$INCOPT" $MAKE -f Makefile.test-ldap test-compile 2>/dev/null
	if test $? -eq 0; then
		echo "Compiling with LDAP works OK"
	else
		echo "WARNING: Cannot compile with LDAP"
		LDAPOK="NO"
	fi

	if test "$LDAPOK" = "YES"
	then
		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-lber clean
		OS=`uname -s | tr '[/]' '[_]'` LDAPINC="$INCOPT" $MAKE -f Makefile.test-lber test-compile 2>/dev/null
		if test $? -eq 0; then
			OS=`uname -s | tr '[/]' '[_]'` LDAPLIB="$LIBOPT" $MAKE -f Makefile.test-lber test-link 2>/dev/null
			if test $? -eq 0; then
				echo "LBER library not needed"
				LDAPLBER=""
			else
				OS=`uname -s | tr '[/]' '[_]'` LDAPLIB="$LIBOPT" LDAPLBER="-llber" $MAKE -f Makefile.test-lber test-link 2>/dev/null
				if test $? -eq 0; then
					echo "LDAP requires the LBER library"
					LDAPLBER="-llber"
				else
					echo "LBER library not found, disabling LDAP support"
					LDAPOK="NO"
				fi
			fi
		else
			echo "WARNING: Cannot compile with LBER, disabling LDAP support"
			LDAPOK="NO"
		fi
	fi

	OS=`uname -s | tr '[/]' '[_]'` LDAPLIB="$LIBOPT" LDAPLBER="$LDAPLBER" $MAKE -f Makefile.test-ldap test-link 2>/dev/null
	if test $? -eq 0; then
		echo "Linking with LDAP works OK"
		LDAPVENDOR=`./test-ldap vendor`
		LDAPVERSION=`./test-ldap version`
		LDAPCOMPILEFLAGS=`./test-ldap flags`
		# echo "LDAP vendor is $LDAPVENDOR, version $LDAPVERSION"
	else
		echo "WARNING: Cannot link with LDAP"
		LDAPOK="NO"
	fi

	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-ldap clean
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-lber clean
	cd ..

	if test "$LDAPOK" = "NO"; then
		echo "(Open)LDAP include- or library-files not found."
		echo "If you want to perform detailed LDAP tests (queries), you need"
		echo "to install an LDAP client library that Xymon can use."
		echo "OpenLDAP can be found at http://www.openldap.org/"
		echo ""
		echo "If you have OpenLDAP installed, use the \"--ldapinclude DIR\" and \"--ldaplib DIR\""
		echo "options to configure to specify where they are."
		echo ""
		sleep 3
		echo "Continuing with LDAP support disabled."
	fi

