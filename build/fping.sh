#!/bin/sh

	echo "Checking for fping ..."

	for DIR in / /usr /usr/local /opt /usr/pkg /opt/csw
	do
		if test -x $DIR/bin/fping
		then
			FPING=$DIR/bin/fping
		elif test -x $DIR/sbin/fping
		then
			FPING=$DIR/sbin/fping
		fi
	done

	if test "$USERFPING" != ""
	then
		FPING="$USERFPING"
	fi

	if test "$FPING" = ""
	then
		echo "Hobbit needs the fping utility. What command should it use to run fping ?"
		read FPING
	else
		echo "Found fping in $FPING"
	fi

	NOTOK=1
	while test $NOTOK -eq 1
	do
		echo "Checking to see if '$FPING 127.0.0.1' works ..."
		$FPING 127.0.0.1
		RC=$?
		if test $RC -eq 0; then
			echo "OK, will use '$FPING' for ping tests"
			echo "NOTE: If you are using an suid-root wrapper, make sure the 'hobbit'"
			echo "      user is also allowed to run fping without having to enter passwords."
			echo "      For 'sudo', add something like this to your 'sudoers' file:"
			echo "      hobbit: ALL=(ALL) NOPASSWD: /usr/local/sbin/fping"
			echo ""
			NOTOK=0
		else
			echo ""
			echo "Failed to run fping."
			echo "If fping is not suid-root, you may want to use an suid-root wrapper"
			echo "like 'sudo' to run fping."
			echo ""
			echo "Hobbit needs the fping utility. What command should it use to run fping ?"
			read FPING
		fi
	done

