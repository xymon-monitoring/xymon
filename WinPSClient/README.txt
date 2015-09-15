What is this?
=============
This is a Xymon monitoring client for Windows systems that have
the "Windows Powershell" scripting tool installed. This is
available for Windows 7, Windows Server 2008, Windows Server 2003
and Windows XP. If it is not installed on your system, it can 
be downloaded from the Microsoft website.

The client utility is completely written in Powershell, so no
additional programs need be installed to run it.


Installation guide
==================

Please see the installation instructions in XymonPSClient.doc.


Client configuration
====================

Please see the configuration instructions in XymonPSClient.doc.


Talking to the Xymon Server
===========================
The "xymonsend.ps1" script contains a Powershell function "XymonSend"
that lets you communicate with the Xymon server in the same way that
the "bb" utility does on the Unix platforms. To use it, you must 
"source" this into your Powershell commandline window: At the "PS"
prompt, enter

	PS C:\xymon> . .\xymonsend.ps1

Note the initial "dot-space" on this line. This defines the function
"XymonSend" in your Powershell instance. You can now use it to talk to
the Xymon server - e.g. ping it (substitute your server name for "xymonhost")

	PS C:\xymon> xymonsend "ping" "xymonhost"
	hobbitd 4.3.0-1.3

You can use all of the normal commands when talking to the server,
e.g. to download a Xymon configuration file you can run

	PS C:\xymon> xymonsend "config bb-hosts" "xymonhost" >bb-hosts

and then you have a copy of the bb-hosts file.
