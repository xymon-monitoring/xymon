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

	return 0
}
