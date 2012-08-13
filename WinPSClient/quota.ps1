# FSRM disk quota monitoring
# David Baldwin - 7/8/2012

param([int]$debug=0,[string]$server="")

# TEST NAME: THIS WILL BECOME A COLUMN ON THE DISPLAY
# IT SHOULD BE AS SHORT AS POSSIBLE TO SAVE SPACE...
# NOTE YOU CAN ALSO CREATE A HELP FILE FOR YOUR TEST
# WHICH SHOULD BE PUT IN www/help/$TEST.html.  IT WILL
# BE LINKED INTO THE DISPLAY AUTOMATICALLY.
#
$TEST="quota"
$TEST2="diskq"
$UsedRedThresh = 95
$UsedYellowThresh = 90

# when updating the quota filter function please update the test of the FilterNote to reflect policy

$FilterNote = "Excluding users with 2GB limit"

function QuotaFilter($quota) {
# function to filter which quotas should not be alerted on or tracked as RRDs
	if($quota.Path -match "\\user\\" -and $quota.Limit -eq 2GB) {
		return $false
	}
	return $true
}

$ipProperties = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
$DOMAIN = ""
if ($ipProperties.DomainName -ne $null) { 
	$DOMAIN = "." + $ipProperties.DomainName
}
$DOMAIN = $DOMAIN.ToLower()

$fsservers = @{
	"c1fs1" = "V";
	"c1fs2" = "U";
	"c1fs3" = "T";
	"c1fs4" = "S";
	"c1fs5" = "R";
	"c1fs6" = "Q";
	"c1fs7" = "P";
	"c1fs8" = "O";
	"c1fs9" = "N";
	"nsw01sv1" = "";
	"vic01sv1" = "";
	"qld01sv1" = "";
	"sa01sv1" = "";
	"wa01sv1" = "";
	"wa02sv1" = "";
}

if($debug) {
  $DebugPreference = "Continue"
} else {
  $WarningPreference = "SilentlyContinue"
}

function Convert-Size([string]$s) {
	if($s -match '(\d+\.?\d*)\s*([KMGT]B)') {
	  return invoke-expression ($Matches[1]+$Matches[2])
	} else {
	  return -1
	}
}

function XymonSend($msg, $servers)
{
	$saveresponse = 1	# Only on the first server
	$outputbuffer = ""
	$ASCIIEncoder = New-Object System.Text.ASCIIEncoding

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

				$saveerractpref = $ErrorActionPreference
				$ErrorActionPreference = "SilentlyContinue"
				$socket = new-object System.Net.Sockets.TcpClient
				$socket.Connect($srvip, $srvport)
				$ErrorActionPreference = $saveerractpref
				if(! $? -or ! $socket.Connected ) {
					$errmsg = $Error[0].Exception
					Write-Error -Category OpenError "Cannot connect to host $srv ($srvip) : $errmsg"
					continue;
				}
				$socket.sendTimeout = 500
				$socket.NoDelay = $true

				$stream = $socket.GetStream()
				
				$sent = 0
				foreach ($line in $msg) {
					# Convert data to ASCII instead of UTF, and to Unix line breaks
					$sent += $socket.Client.Send($ASCIIEncoder.GetBytes($line.Replace("`r","") + "`n"))
				}

				if ($saveresponse-- -gt 0) {
					$socket.Client.Shutdown(1)	# Signal to Xymon we're done writing.

					$s = new-object system.io.StreamReader($stream,"ASCII")

					start-sleep -m 200  # wait for data to buffer
					$outputBuffer = $s.ReadToEnd()
				}

				$socket.Close()
			}
		}
	}
	$outputbuffer
}

function checkQuota($server,$drive="") {
	write-debug "srv=$server drv=$drive"
	$FilterHit = 0
	$MACHINE = $server + $DOMAIN -replace "\.",","
	$COLOR="green"
	$STATUS="Quotas OK"
	$WARNINGS = ""
	$RRDdata = ""
	$linefmt = "`n{0,-4} {1,-40}`n       {2,11} {3,11} {4,11} {5,5:0} {6,-6} {7,-2} {8,-11} {9}"
	$LINE = ($linefmt -f "  ","Path","Limit","Used","Free","%Used","Type","On","Peak","PeakTime")

	$dqpath = if($drive -ne "") { "/path:$($drive):\..." } else { "" }
	write-debug "DQ $dqpath"
	$dql = DIRQUOTA.EXE QUOTA LIST /REMOTE:$server $dqpath
	$ql = switch -regex ($dql) {
		 '^Quota Path:\s+((\w):.*)' {$Path = $Matches[1]; $Drive = $Matches[2]}
		 '^Source Template:\s+(\S.*)' {$SourceTemplate = $Matches[1]}
		 '^Label:\s+(\S.*)' {$Label = $Matches[1]}
		 '^Limit:\s+(\d.*\w)\s+\((\w+)\)' {$Limit = Convert-Size $Matches[1]; $LimitTxt = $Matches[1]; $QType = $Matches[2]}
		 '^Used:\s+(\d.*\w)\s+\((\d+)%\)' {$Used = Convert-Size $Matches[1]; $UsedTxt = $Matches[1]; $UsedPct = [int]$Matches[2]}
		 '^Available:\s+(\d.*)' {$Available = Convert-Size $Matches[1]; $AvailableTxt = $Matches[1]}
		 '^Peak Usage:\s+(\d.*\w)\s+\((.*)\)' {$PeakUsage = Convert-Size $Matches[1]; $PeakUsageTxt = $Matches[1]; $PeakDate = [datetime]::Parse($Matches[2])}
		 '^Quota Status:\s+(\S*)' {$QStatus = $Matches[1]}
		 '^\s+Warning\s+\((\d+)%\):\s+(\S.*)' {$WarningPct = $Matches[1]; $WarningAction = $Matches[2]}
		 '^\s+Limit\s+\((\d+)%\):\s+(\S.*)' {$LimitPct = $Matches[1]; $LimitAction = $Matches[2]}
		 '^\s*$' {
			if($Path -ne $null) { New-Object PSObject -Property @{
				Host = $server
				Path = $Path
				Drive = $Drive
				SourceTemplate = $SourceTemplate
				Label = $Label
				Limit = $Limit
				LimitTxt = $LimitTxt
				QType = $QType
				Used = $Used
				UsedTxt = $UsedTxt
				UsedPct = $UsedPct
				Available = $Available
				AvailableTxt = $AvailableTxt
				PeakUsage = $PeakUsage
				PeakUsageTxt = $PeakUsageTxT
				PeakDate = $PeakDate
				Status = $QStatus
				WarningPct = $WarningPct
				WarningAction = $WarningAction
				LimitPct = $LimitPct
				LimitAction = $LimitAction
				}
			}
		}
	}
	$drivetot = @{}
	$quotatot = @{}
	$ql | sort-object -Property Path | %{
		write-debug $_
		$DBCOL = "&green"
		if ($_.UsedPct -ge $UsedRedThresh) {
			$DBCOL = "&red"
			if(QuotaFilter $_) {
				$COLOR = "red"
				$STATUS="Quota not OK"
				$WARNINGS+=("&red Quota $($_.Path) at {0:0}%`n" -f $_.UsedPct)
			}
		} elseif ($_.UsedPct -ge $UsedYellowThresh) {
			$DBCOL = "&yellow"
			if(QuotaFilter $_) {
				if ($COLOR -eq "green") { $COLOR = "yellow" }
				$STATUS="Quota not OK"
				$WARNINGS+=("&yellow Quota $($_.Path) at {0:0}%`n" -f $_.UsedPct)
			}
		}
		if($drivetot[$_.Drive] -eq $null) { $drivetot[$_.Drive] = @{} }
		$drivetot[$_.Drive]["Limit"] += $_.Limit
		$drivetot[$_.Drive]["Used"] += $_.Used
		$drivetot[$_.Drive]["Available"] += $_.Available
		$RRDfmt = "[$TEST.{0}.rrd]`nDS:limit:GAUGE:600:0:U {1:0}`nDS:used:GAUGE:600:0:U {2:0}`nDS:free:GAUGE:600:0:U {3:0}`nDS:peak:GAUGE:600:0:U {4:0}`n"
		if(QuotaFilter $_) {
			$rrdpath = (($_.Path -replace " ","_") -replace "\\","|")
			$qfree = $_.Available
			if($qfree -lt 0) {$qfree = 0}
			$RRDdata += ($RRDfmt -f $rrdpath, $_.Limit, $_.Used, $qfree, $_.PeakUsage)
		} else {
			$FilterHit++
		}
#	$LINE = ($linefmt -f "  ","Path","Limit","Used","Free","%Free","Type","Peak","PeakTime")
		$qon = if($_.Status -eq "Enabled") { "Y" } else { "N" }
		$LINE += ($linefmt -f $DBCOL, $_.Path, $_.LimitTxt, $_.UsedTxt, $_.AvailableTxt, $_.UsedPct, $_.QType, $qon, $_.PeakUsageTxt, $_.PeakDate)
	 }
	 if($FilterHit -gt 0) { $LINE += "`nNOTE: $FilterNote - count=$FilterHit excluded from alerts and graphs`n"}
	 $DISKWARN = ""
	 $STATUS2 = "Disk OK"
	 $COLOR2 = "green"
	 $DISKFMT = "{0,5} {1,9:0} {2,9:0} {3,9:0} {4,9:0} {5,9:0}`n"
	 $DISKLINE = $DISKFMT -f "Drive","Committed", "Used", "Available", "Size", "Free"
	 foreach($dr in ($drivetot.keys + $drive | sort -unique)) {
		$drvstats = Get-WmiObject win32_LogicalDisk -filter ("Name='"+$dr+":'") -computername $node
		$drlim = $drivetot[$dr]["Limit"]/1GB
		if($drlim -eq $null) {$drlim = 0}
		$drused = $drivetot[$dr]["Used"]/1GB + 0
		if($drused -eq $null) {$drused = 0}
		$dravail = $drivetot[$dr]["Available"]/1GB + 0
		if($dravail -eq $null) {$dravail = 0}
		$DISKLINE += $DISKFMT -f $dr,$drlim,$drused,$dravail,($drvstats.Size/1GB),($drvstats.Freespace/1GB)
		$RRDfmt = "[$TEST2.{0}.rrd]`nDS:limit:GAUGE:600:0:U {1:0}`nDS:used:GAUGE:600:0:U {2:0}`nDS:free:GAUGE:600:0:U {3:0}`nDS:size:GAUGE:600:0:U {4:0}`n"
		$RRDdata += ($RRDfmt -f $dr, $drlim,$drused, $drvstats.Freespace, $drvstats.Size)
	 }
	 $LINE = $DISKLINE + $LINE
	#
	# AT THIS POINT WE HAVE OUR RESULTS.  NOW WE HAVE TO SEND IT TO
	# THE BBDISPLAY TO BE DISPLAYED...
	#
	$date = get-date -format "ddd MMM dd HH':'mm':'ss zzz yyyy"
	if($debug) {
	  echo "status $MACHINE.$TEST $COLOR $date - $STATUS`n$WARNINGS`n$LINE"
	  echo "status $MACHINE.$TEST2 $COLOR2 $date - $STATUS2`n$DISKWARN`n$DISKLINE"
	  echo "data $MACHINE.trends`n$RRDdata"
	}

	if($debug -le 1) {
	  $dummy = XymonSend "status $MACHINE.$TEST $COLOR $date - $STATUS`n$WARNINGS`n$LINE" $servers
	  $dummy = XymonSend "status $MACHINE.$TEST2 $COLOR2 $date - $STATUS2`n$DISKWARN`n$DISKLINE" $servers
	  $dummy = XymonSend "data $MACHINE.trends`n$RRDdata" $servers
	}
}

$BBWinRegKey = $(if([System.IntPtr]::Size -eq 8) { "HKLM:\SOFTWARE\Wow6432Node\BBWin" } else { "HKLM:\SOFTWARE\BBWin" })
$BBWinSettings = Get-ItemProperty -ErrorAction:SilentlyContinue $BBWinRegKey
if($BBWinSettings -eq $null) {
	write-error -Category ObjectNotFound "No BBWin reg hive found: $BBWinRegKey"
	return
} else {
	$etcpath = $BBWinSettings.etcpath
	write-debug "etcpath = $etcpath"
	write-debug ""
	$BBWinCfg = join-path $etcpath "BBWin.cfg"
	if(test-path $BBWinCfg) {
		$cfg = [xml](get-content $BBWinCfg)
		$servers = $cfg.configuration.bbwin.setting | ? {$_.Name -eq "bbdisplay"} | %{$_.value}
		write-debug "servers = $servers"
		write-debug ""
	} else {
		write-error -Category OpenError "Not found BBWin.cfg: $BBWincfg"
		return
	}
}
$serverlist = if($server -ne "") { $server } else { $fsservers.keys }
foreach ($node in $serverlist) {
	$srvdrive = if($fsservers[$node] -ne $null) { $fsservers[$node] } else { "" }
	checkQuota $node $srvdrive
}