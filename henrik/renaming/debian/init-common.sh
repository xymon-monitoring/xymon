# common init functions for Xymon and Xymon-client

create_includefiles ()
{
	if [ "$XYMONSERVERS" = "" ]; then
		echo "Please configure XYMONSERVERS in /etc/default/xymon-client"
		exit 0
	fi

	umask 022

	if ! [ -d /var/run/xymon ] ; then
		mkdir /var/run/xymon
		chown xymon:xymon /var/run/xymon
	fi

	set -- $XYMONSERVERS
	if [ $# -eq 1 ]; then
		echo "BBDISP=\"$XYMONSERVERS\""
		echo "BBDISPLAYS=\"\""
	else
		echo "BBDISP=\"0.0.0.0\""
		echo "BBDISPLAYS=\"$XYMONSERVERS\""
	fi > /var/run/xymon/bbdisp-runtime.cfg

	for cfg in /etc/xymon/clientlaunch.d/*.cfg ; do
		test -e $cfg && echo "include $cfg"
	done > /var/run/xymon/clientlaunch-include.cfg

	if test -d /etc/xymon/xymonlaunch.d ; then
		for cfg in /etc/xymon/xymonlaunch.d/*.cfg ; do
			test -e $cfg && echo "include $cfg"
		done > /var/run/xymon/xymonlaunch-include.cfg
	fi

	if test -d /etc/xymon/xymongraph.d ; then
		for cfg in /etc/xymon/xymongraph.d/*.cfg ; do
			test -e $cfg && echo "include $cfg"
		done > /var/run/xymon/xymongraph-include.cfg
	fi

	if test -d /etc/xymon/xymonserver.d ; then
		for cfg in /etc/xymon/xymonserver.d/*.cfg ; do
			test -e $cfg && echo "include $cfg"
		done > /var/run/xymon/xymonserver-include.cfg
	fi

	if test -d /etc/xymon/xymonclient.d ; then
		for cfg in /etc/xymon/xymonclient.d/*.cfg ; do
			test -e $cfg && echo "include $cfg"
		done > /var/run/xymon/xymonclient-include.cfg
	fi

	return 0
}
