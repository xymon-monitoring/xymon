function XymonSend($msg, $servers)
{
	$saveresponse = 1	# Only on the first server

	$ASCIIEncoder = New-Object System.Text.ASCIIEncoding

	foreach ($srv in $servers) {
		$srvparams = $srv.Split(":")
		$srvip = $srvparams[0]
		if ($srvparams.Count -gt 1) {
			$srvport = $srvparams[1]
		}
		else {
			$srvport = 1984
		}

		try {
			$socket = new-object System.Net.Sockets.TcpClient($srvip, $srvport)
		}
		catch {
			$errmsg = $Error[0].Exception
			Write-Error "Cannot connect to host $srv : $errmsg"
			continue;
		}

		$stream = $socket.GetStream() 
		
		foreach ($line in $msg)
		{
			# Convert data to ASCII instead of UTF, and to Unix line breaks
			$sent += $socket.Client.Send($ASCIIEncoder.GetBytes($line.Replace("`r","") + "`n"))
		}

		if ($saveresponse) {
			$socket.Client.Shutdown(1)	# Signal to Xymon we're done writing.

		    $buffer = new-object System.Byte[] 4096
			$encoding = new-object System.Text.AsciiEncoding
			$outputBuffer = ""

    		do {
        		## Allow data to buffer for a bit
        		start-sleep -m 200

        		## Read what data is available
        		$foundmore = $false
        		$stream.ReadTimeout = 1000

        		do {
            		try {
                		$read = $stream.Read($buffer, 0, 1024)

                		if ($read -gt 0) {
                    		$foundmore = $true
                    		$outputBuffer += ($encoding.GetString($buffer, 0, $read))
                		}
					}
					catch { 
						$foundMore = $false; $read = 0
					}
        		} while ($read -gt 0)
    		} while ($foundmore)
		}

		$socket.Close()
	}

	$outputbuffer
}
