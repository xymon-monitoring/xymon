# common init functions for xymon and xymon-client

create_includefiles ()
{
	if [ "$XYMONSERVERS" = "" ]; then
		echo "Please configure XYMONSERVERS in /etc/default/xymon-client"
		exit 0
	fi

	umask 022

	if ! [ -d /run/xymon ] ; then
		mkdir /run/xymon
		chown xymon:xymon /run/xymon
	fi

	set -- $XYMONSERVERS
	if [ $# -eq 1 ]; then
		echo "XYMSRV=\"$XYMONSERVERS\""
		echo "XYMSERVERS=\"\""
	else
		echo "XYMSRV=\"0.0.0.0\""
		echo "XYMSERVERS=\"$XYMONSERVERS\""
	fi > /run/xymon/bbdisp-runtime.cfg

	return 0
}
