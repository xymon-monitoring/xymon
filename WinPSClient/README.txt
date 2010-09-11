What is this?
=============
This is a Xymon monitoring client for Windows systems that have
the "Windows Powershell" scripting tool installed. This is
available for Windows 7, Windows Server 2008, Windows Server 2003
and Windows XP. If it is not installed on your system, it can 
be downloaded from the Microsoft website.

The client utility is completely written in Powershell, so no
additional programs need be installed to run it. However, you
will need the Windows Resource Toolkit to install the client
as a Windows Service.


Installation guide
==================

Unpack the distribution zip-file to C:\ . All of the
files unpack into the C:\Xymon directory.

You must configure the IP-address or hostname of
your Xymon server - set this at the top of the
c:\Xymon\xymonclient.ps1

	# $clientname  = "winxptest"
	$xymonserver = @( "xymonhost" )

Modify the "172.16.10.2" IP-address to be the address
of your Xymon server.

By default the client sends data using the system hostname.
If you want it to report using a different name, you can
set this explicitly in the "clientname". Uncomment the line,
and change "winxptest" to the hostname you want to use.

If you want to run it as a service (and you probably do),
then you must use the "Instsrv.exe" and "SrvAny.exe"
utilities from the Windows Resource Kit Tools. 
You can download this from Microsoft:

http://www.microsoft.com/downloads/en/details.aspx?familyid=9d467a69-57ff-4ae7-96ee-b18c4790cffd&displaylang=en

1) Install the Resource Kit Tools. 

2) From a command window (i.e. "cmd.exe") run:

	"C:\Program Files\Windows Resource Kits\Tools\Instsrv.exe" XymonClient "C:\Program Files\Windows Resource Kits\Tools\Srvany.exe"

3) Run the registry editor RegEdit32 and find the key 

	HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\XymonClient

   From the Edit menu, click "Add Key", name the new key "Parameters".

   Select the "Parameters" key. Then from the Edit menu, click "Add String". 
   Name the new string "Application".

   Double-click the new "Application" entry, and enter the following in 
   the "Value" field:

	c:\windows\system32\windowspowershell\v1.0\powershell.exe c:\xymon\xymonclient.ps1

   Close the registry editor.

4) From the Windows Service manager, select the "XymonClient" service and start it.


NOTE: The "C:\Program Files" directory may be different, if you
have installed Windows on another disk, and/or if you are running
a non-english version of Windows. Adjust the commands to match
your local settings.


For more information, refer to the Microsoft documentation for
instsrv.exe and srvany.exe in their KB article 137890:
http://support.microsoft.com/kb/137890


Uninstalling the service
========================

To remove the service, first make sure it has been stopped. From a
commandline windows (cmd.exe), run:

	net stop XymonClient

You can then either use "InstSrv.exe" from the Resource Kit:

	C:\Program Files\Windows Resource Kits\Tools\instsrv.exe XymonClient REMOVE

or the "sc" utility:

	sc delete XymonClient


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
the Xymon server - e.g. ping it:

	PS C:\xymon> xymonsend "ping" "xymonhost"
	hobbitd 4.3.0-1.3

You can use all of the normal commands when talking to the server,
e.g. to download a Xymon configuration file you can run

	PS C:\xymon> xymonsend "config bb-hosts" "xymonhost" >bb-hosts

and then you have a copy of the bb-hosts file.
