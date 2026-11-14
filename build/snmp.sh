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
			# net-snmp-config exports the distro's full build policy along
			# with the interface: -Werror=declaration-after-statement (newer
			# net-snmp), the EL9 hardened-ld -specs (forces PIE links against
			# our non-PIC objects), LTO, fortify options, and more. Keep only
			# what we actually need from it: for compiling, include dirs and
			# defines; for linking, library names, dirs and runtime paths.
			SNMPINCDIR=""
			for flag in `net-snmp-config --cflags`
			do
				case $flag in
					-I*|-D*|-U*|-pthread) SNMPINCDIR="$SNMPINCDIR $flag" ;;
				esac
			done
			SNMPLIBS=""
			for flag in `net-snmp-config --libs`
			do
				case $flag in
					-l*|-L*|-Wl,-rpath*|-Wl,-R*|-pthread) SNMPLIBS="$SNMPLIBS $flag" ;;
				esac
			done
			SNMPOK="YES"
		else
			echo "Could not find Net-SNMP (net-snmp-config command fails)"
			echo "SNMP support will not be available."
		fi
	fi

