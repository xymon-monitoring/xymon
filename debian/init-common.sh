# common init functions for xymon and xymon-client

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
		echo "XYMSRV=\"$XYMONSERVERS\""
		echo "XYMSERVERS=\"\""
	else
		echo "XYMSRV=\"0.0.0.0\""
		echo "XYMSERVERS=\"$XYMONSERVERS\""
	fi > /var/run/xymon/bbdisp-runtime.cfg

	for cfg in /etc/xymon/clientlaunch.d/*.cfg ; do
		test -e $cfg && echo "include $cfg"
	done > /var/run/xymon/clientlaunch-include.cfg

	for cfg in /etc/xymon/xymonclient.d/*.cfg ; do
		test -e $cfg && echo "include $cfg"
	done > /var/run/xymon/xymonclient-include.cfg

	if test -x /usr/lib/xymon/server/bin/xymond ; then
		for cfg in /etc/xymon/tasks.d/*.cfg ; do
			test -e $cfg && echo "include $cfg"
		done > /var/run/xymon/tasks-include.cfg

		for cfg in /etc/xymon/graphs.d/*.cfg ; do
			test -e $cfg && echo "include $cfg"
		done > /var/run/xymon/graphs-include.cfg

		for cfg in /etc/xymon/xymonserver.d/*.cfg ; do
			test -e $cfg && echo "include $cfg"
		done > /var/run/xymon/xymonserver-include.cfg
	fi

	return 0
}
