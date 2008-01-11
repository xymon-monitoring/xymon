	echo "Checking for Net-SNMP ..."

	SNMPINC=""
	SNMPLIB=""

	VERSION=`net-snmp-config --version`
	if test $? -eq 0
	then
		echo "Found Net-SNMP version $VERSION"
		DOSNMP=yes
	else
		sleep 3
		echo "Could not find Net-SNMP (net-snmp-config command fails)"
		echo "Continuing with SNMP support disabled."
		DOSNMP=no
	fi

