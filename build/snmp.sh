	echo "Checking for Net-SNMP ..."

	SNMPOK="NO"
	SNMPINCDIR=""
	SNMPLIBS=""

	if test "$USERSNMPINC" != "" -o "$USERSNMPLIB" != ""
	then
		# Location specified explicitly via --snmpinclude / --snmplib.
		if test "$USERSNMPINC" != ""; then SNMPINCDIR="-I$USERSNMPINC"; fi
		if test "$USERSNMPLIB" != ""; then SNMPLIBS="-L$USERSNMPLIB -lnetsnmp"; fi
		SNMPOK="YES"
		echo "Using user-specified Net-SNMP location"
	else
		VERSION=`net-snmp-config --version 2>/dev/null`
		if test $? -eq 0
		then
			echo "Found Net-SNMP version $VERSION"
			SNMPINCDIR=`net-snmp-config --cflags`
			SNMPLIBS=`net-snmp-config --libs`
			SNMPOK="YES"
		else
			echo "Could not find Net-SNMP (net-snmp-config command fails)"
			echo "SNMP support will not be available."
		fi
	fi

