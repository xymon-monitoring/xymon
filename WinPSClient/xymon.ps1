#ident  "@(#)xymon.ps1       0.2.1   2017-11-21 abs"
#******************************************************************************
# $Id$
# $Revision$
# $Author$
# $Date$
# $HeadURL$
#******************************************************************************
# powershell version of native UNIX utility to send messages to any Xymon
# server or proxy using the Big Brother protocol
#
#   0.0.1 2016-04-07 abs      test version (aka xymoncmd.ps1)
#   0.1.0 2017-10-27 abs      C/L recipient supersedes configuration
#   0.2.0 2017-11-20 abs      special handling for download command
#   0.2.1 2017-11-21 abs      backport XymonSend from xymonclient 2.21
#                             including dependant functions.  Implement (but
#                             dont document) -log.
#                             better checking for output file
#******************************************************************************
#
param ( 
         [Parameter(Mandatory=$false)][string]$recipient,
         [Parameter(Mandatory=$false)][string]$message,
         [Parameter(Mandatory=$false)][string]$msgfile,
         [Parameter(Mandatory=$false)][string]$output,
         [Parameter(Mandatory=$false)][switch]$help,
         [Parameter(Mandatory=$false)][switch]$log
)

function Usage()
{
  Write-Host "Usage : $xymoncmd [-recipient RECIPIENT] -message 'DATA'|-msgfile FILE"
  Write-Host "        $xymoncmd [-recipient RECIPIENT] -message 'download FILE' [-output RECEIVED]"
  Write-Host ""
  Write-Host "   RECIPIENT: IP-address or hostname (overrides configured value)"
  Write-Host "   DATA: Message to send, or '@' to read from stdin"
  Write-Host ""
  Write-Host "One of either -message or -msgfile must be provided and if using the latter,"
  Write-Host "then FILE will be deleted after successfull transmission of DATA!"
  Write-Host ""
  Write-Host "For the special case of the message 'download FILE' then the received"
  Write-Host "data will be saved locally in 'FILE' but this can be overridden using"
  Write-Host "the -output option"
  exit 0
}

function XymonSend($msg, $servers, $filePath)
{
    $saveresponse = 1   # Only on the first server
    $outputbuffer = ""
    if ($script:XymonSettings.XymonAcceptUTF8 -eq 1) 
    {
        WriteLog 'Using UTF8 encoding'
        $MessageEncoder = New-Object System.Text.UTF8Encoding
    }
    else 
    {
        WriteLog 'Using ASCII encoding'
        $MessageEncoder = New-Object System.Text.ASCIIEncoding
        # remove diacritics
        $msg = Remove-Diacritics -String $msg
        # convert non-break spaces to normal spaces
        $msg = $msg.Replace([char]0x00a0,' ')
    }

    foreach ($srv in $servers) {
        $srvparams = $srv.Split(":")
        # allow for server names that may resolve to multiple A records
        $srvIPs = & {
            $local:ErrorActionPreference = "SilentlyContinue"
            $srvparams[0] | %{[system.net.dns]::GetHostAddresses($_)} | %{ $_.IPAddressToString}
        }
        if ($srvIPs -eq $null) { # no IP addresses could be looked up
            Write-Error -Category InvalidData ("No IP addresses could be found for host: " + $srvparams[0])
        } else {
            if ($srvparams.Count -gt 1) {
                $srvport = $srvparams[1]
            } else {
                $srvport = 1984
            }
            foreach ($srvip in $srvIPs) {

                WriteLog "Connecting to host $srvip"

                $saveerractpref = $ErrorActionPreference
                $ErrorActionPreference = "SilentlyContinue"
                $socket = new-object System.Net.Sockets.TcpClient
                $socket.Connect($srvip, $srvport)
                $ErrorActionPreference = $saveerractpref
                if(! $? -or ! $socket.Connected ) {
                    $errmsg = $Error[0].Exception
                    WriteLog "ERROR: Cannot connect to host $srv ($srvip) : $errmsg"
                    Write-Error -Category OpenError "Cannot connect to host $srv ($srvip) : $errmsg"
                    continue;
                }
                $socket.sendTimeout = 500
                $socket.NoDelay = $true

                $stream = $socket.GetStream()
                
                $sent = 0
                foreach ($line in $msg) {
                    # Convert data as appropriate
                    try
                    {
                        $sent += $socket.Client.Send($MessageEncoder.GetBytes($line.Replace("`r","") + "`n"))
                    }
                    catch
                    {
                        WriteLog "ERROR: $_"
                    }
                }
                WriteLog "Sent $sent bytes to server"

                if ($saveresponse-- -gt 0) {
                    $socket.Client.Shutdown(1)  # Signal to Xymon we're done writing.

                    $bytes = 0
                    $line = ($msg -split [environment]::newline)[0]
                    $line = $line -replace '[\t|\s]+', ' '
                    if  ($line -match '(download) (.*$)' ) {
                        if ($filePath -eq $null -or $filePath -eq "") {
                            # save it locally with the same name
                            $filePath = split-path -leaf $matches[2]
                        }
                        $buffer = new-object System.Byte[] 2048;
                        $fileStream = New-Object System.IO.FileStream($filePath, [System.IO.FileMode]'Create', [System.IO.FileAccess]'Write');

                        do
                        {
                            $read = $null;
                            while($stream.DataAvailable -or $read -eq $null) {
                                $read = $stream.Read($buffer, 0, 2048);
                                if ($read -gt 0) {
                                    $fileStream.Write($buffer, 0, $read);
                                    $bytes += $read
                                }
                            }
                        } while ($read -gt 0);
                        $fileStream.Close();
                        WriteLog "Wrote $bytes bytes from server to $filePath"

                    } else {
                        $s = new-object system.io.StreamReader($stream,"ASCII")

                        start-sleep -m 200  # wait for data to buffer
                        try
                        {
                            $outputBuffer = $s.ReadToEnd()
                            WriteLog "Received $($outputBuffer.Length) bytes from server"
                        }
                        catch
                        {
                            WriteLog "ERROR: $_"
                        }
                    }
                }

                $socket.Close()
            }
        }
    }
    $outputbuffer
}

function SetIfNot($obj,$key,$value) {
    if($obj.$key -eq $null) { $obj | Add-Member -MemberType noteproperty -Name $key -Value $value }
}

function XymonConfig {
    if (Test-Path $XymonClientCfg) {
        XymonInitXML
        $script:XymonCfgLocation = "XML: $XymonClientCfg"
    } else {
        XymonInitRegistry
        $script:XymonCfgLocation = "Registry"
    }
    XymonInit
}

function XymonInitXML {
    $xmlconfig = [xml](Get-Content $XymonClientCfg)
    $script:XymonSettings = $xmlconfig.XymonSettings
}

function XymonInitRegistry {
    $script:XymonSettings = Get-ItemProperty -ErrorAction:SilentlyContinue $XymonRegKey
}

function XymonInit {
    if($script:XymonSettings -eq $null) {
        $script:XymonSettings = New-Object Object
    } 

    if ($recipient -eq '') {
        $servers = $script:XymonSettings.servers
        SetIfNot $script:XymonSettings serversList $servers
        if ($script:XymonSettings.servers -match " ") {
            $script:XymonSettings.serversList = $script:XymonSettings.servers.Split(" ")
        }
        if ($script:XymonSettings.serversList -eq $null) {
            SetIfNot $script:XymonSettings serversList $recipient
        }
    } else {
        SetIfNot $script:XymonSettings serversList $recipient
    }
    $extdata = Join-Path $xymondir 'tmp'
    SetIfNot $script:XymonSettings externaldatalocation $extdata
    SetIfNot $script:XymonSettings XymonAcceptUTF8 0 # messages sent to Xymon 0 = convert to ASCII, 1 = convert to UTF8
    SetIfNot $script:XymonSettings clientlogfile "$env:TEMP\xymonclient.log" # path for logfile

}

function WriteLog([string]$message)
{
  if ( $log ) {
    $datestamp = get-date -uformat '%Y-%m-%d %H:%M:%S'
    if ( Test-Path $script:XymonSettings.clientlogfile ) {
      add-content -Path $script:XymonSettings.clientlogfile -Value "$datestamp  $message"
    }
    Write-Host "$datestamp  $message"
  }
}

# from http://poshcode.org/1054
function Remove-Diacritics([string]$String) 
{
    $objD = $String.Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    for ($i = 0; $i -lt $objD.Length; $i++) 
    {
        $c = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($objD[$i])
        if($c -ne [Globalization.UnicodeCategory]::NonSpacingMark) 
        {
            [void]$sb.Append($objD[$i])
        }
    }
    return("$sb".Normalize([Text.NormalizationForm]::FormC))
}

$xymoncmd = split-path -leaf $MyInvocation.MyCommand.Definition
if ($help ) {
  Usage
}

$xymondir = split-path -parent $MyInvocation.MyCommand.Definition
$XymonClientCfg = join-path $xymondir 'xymonclient_config.xml'
XymonConfig

$ret = 0

if ($message -eq '@') {
  $message = [Console]::In.ReadToEnd()
}

if ($output -ne '' -and $output -ne $null ) {
  # check output for sanity
  $destination = split-path -parent $output
  if ($destination -ne '' -and $destination -ne $null ) {
    if (!(Test-Path -PathType Container $destination)) {
      throw "$destination : no such directory"
    }
  }
}

if ($message -eq '') {
  if ($msgfile -ne '') {
    if (Test-Path $msgfile) {
      $message = [IO.File]::ReadAllText($msgfile)
    } else {
      throw "$msgfile : no such file or directory"
    }
  }
}

if ($message -ne '') {
  $ret = XymonSend $message $script:XymonSettings.serversList $output
  if ($ret -ne '') {
    Write-Host -NoNewline $ret
  }
  if ($msgfile -ne '') {
    $msgfile | Remove-Item -force
  }
} else {
  Write-Host "No message to send"
  Usage
}
