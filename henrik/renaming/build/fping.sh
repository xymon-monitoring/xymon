#!/bin/sh

	echo "Checking for fping ..."

	for DIR in / /usr /usr/local /opt /usr/pkg /opt/csw
	do
		if test "$DIR" = "/"; then DIR=""; fi

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

	if test "$USEXYMONPING" = ""
	then
		echo "Xymon has a built-in ping utility (hobbitping)"
		echo "However, it is not yet fully stable and therefore it"
		echo "may be best to use the external fping utility instead."
		if test "$FPING" = ""
		then
			echo "I could not find fping on your system"
			echo "Do you want to use hobbitping [Y/n] ?"
			read USEXYMONPING
			if test "$USEXYMONPING" = "n"
			then
				echo "What command should Xymon use to run fping ?"
				read FPING
			else
				USEXYMONPING="y"
				echo "OK, I will use hobbitping."
				FPING="hobbitping"
			fi
		else
			echo "I found fping in $FPING"
			echo "Do you want to use it [Y/n] ?"
			read USEFPING
			if test "$USEFPING" = "n"
			then
				USEXYMONPING="y"
				echo "OK, I will use hobbitping instead."
				FPING="hobbitping"
			fi
		fi
	elif test "$USEXYMONPING" = "n"
	then
		echo "OK, will use '$FPING' for ping tests"
	else
		FPING="hobbitping"
		USEXYMONPING="y"
	fi

	if test "$USEXYMONPING" = "y" -o "$USERFPING" != ""
	then
		NOTOK=0
	else
		NOTOK=1
	fi

	while test $NOTOK -eq 1
	do
		echo "Checking to see if '$FPING 127.0.0.1' works ..."
		$FPING 127.0.0.1
		RC=$?
		if test $RC -eq 0; then
			echo "OK, will use '$FPING' for ping tests"
			echo "NOTE: If you are using an suid-root wrapper, make sure the 'xymond'"
			echo "      user is also allowed to run fping without having to enter passwords."
			echo "      For 'sudo', add something like this to your 'sudoers' file:"
			echo "      xymon: ALL=(ALL) NOPASSWD: /usr/local/sbin/fping"
			echo ""
			NOTOK=0
		else
			echo ""
			echo "Failed to run fping."
			echo "If fping is not suid-root, you may want to use an suid-root wrapper"
			echo "like 'sudo' to run fping."
			echo ""
			echo "Xymon needs the fping utility. What command should it use to run fping ?"
			read FPING
		fi
	done

