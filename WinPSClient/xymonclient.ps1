# ###################################################################################
# 
# Xymon client for Windows
#
# This is a client implementation for Windows systems that support the
# Powershell scripting language.
#
# Copyright (C) 2010 Henrik Storner <henrik@hswn.dk>
# Copyright (C) 2010 David Baldwin
#
# This program is released under the GNU General Public License (GPL),
# version 2. See the file "COPYING" for details.
#
# ###################################################################################

# -----------------------------------------------------------------------------------
# User configurable settings
# -----------------------------------------------------------------------------------

$xymonservers = @( "xymonhost" )	# List your Xymon servers here
# $clientname  = "winxptest"	# Define this to override the default client hostname


$xymonsvcname = "XymonPSClient"
$xymondir = (get-item $MyInvocation.InvocationName).DirectoryName
# -----------------------------------------------------------------------------------

$XymonClientVersion = "$Id$"

function SetIfNot($obj,$key,$value)
{
    if($obj.$key -eq $null) { $obj | Add-Member -MemberType noteproperty -Name $key -Value $value }
}

function XymonInit
{
	# Get reg key first, then override if not set
	$script:XymonSettings = Get-ItemProperty  -ErrorAction:SilentlyContinue HKLM:\SOFTWARE\XymonPSClient
	if($script:XymonSettings -eq $null) {
		$script:XymonSettings = New-Object Object
	} else { # any special handling for settings from reg keys
		if($XymonSettings.servers -match " ") {
			$XymonSettings.servers = $XymonSettings.servers.Split(" ")
		}
	}
	SetIfNot $XymonSettings servers $xymonservers # List your Xymon servers here
	# SetIfNot $XymonSettings clientname "winxptest"	# Define this to override the default client hostname

	# Params for default clientname
	SetIfNot $XymonSettings clientfqdn 1 # 0 = unqualified, 1 = fully-qualified
	SetIfNot $XymonSettings clientlower 1 # 0 = unqualified, 1 = fully-qualified
	if ($XymonSettings.clientname -eq $null -or $XymonSettings.clientname -eq "") { # set name based on rules
		$ipProperties = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
		$clname  = $ipProperties.HostName
		if ($XymonSettings.clientfqdn -and ($ipProperties.DomainName -ne $null)) { 
			$clname += "." + $ipProperties.DomainName
		}
		if ($XymonSettings.clientlower) { $clname = $clname.ToLower() }
		if($XymonSettings.clientname -eq "") {$XymonSettings.clientname = $clname }
		else {SetIfNot $XymonSettings clientname $clname}
	}

	# Params for various client options
	SetIfNot $XymonSettings clientbbwinmembug 1 # 0 = report correctly, 1 = page and virtual switched
	SetIfNot $XymonSettings clientremotecfgexec 0 # 0 = don't run remote config, 1 = run remote config
	SetIfNot $XymonSettings clientconfigfile "$env:TEMP\xymonconfig.cfg" # path for saved client-local.cfg section from server
	SetIfNot $XymonSettings clientlogfile "$env:TEMP\xymonclient.log" # path for logfile
	SetIfNot $XymonSettings loopinterval 300 # seconds to repeat client reporting loop
	SetIfNot $XymonSettings maxlogage 60 # minutes age for event log reporting
	SetIfNot $XymonSettings slowscanrate 72 # repeats of main loop before collecting slowly changing information again

	$script:wanteddisks = @( 3 )	# 3=Local disks, 4=Network shares, 2=USB, 5=CD
	$script:wantedlogs = "Application",  "System", "Security"
    $script:clientlocalcfg = ""
	$script:logfilepos = @{}

	
	$script:HaveCmd = @{}
	foreach($cmd in "query","qwinsta") {
		$script:HaveCmd.$cmd = (get-command -ErrorAction:SilentlyContinue $cmd) -ne $null
	}

	@("cpuinfo","totalload","numcpus","numcores","numvcpus","osinfo","svcs","procs","disks",`
	"netifs","svcprocs","localdatetime","uptime","usercount",`
	"XymonProcsCpu","XymonProcsCpuTStart","XymonProcsCpuElapsed") `
	| %{ if (get-variable -erroraction SilentlyContinue $_) { Remove-Variable $_ }}
	
	if ((get-variable -erroraction SilentlyContinue "clientname") -eq $null -or $script:clientname -eq "") {
		$ipProperties = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
		$script:clientname  = $ipProperties.HostName
		if ($XymonSettings.clientfqdn -and ($ipProperties.DomainName -ne $null)) { 
			$script:clientname += "." + $ipProperties.DomainName
		}
		if ($XymonSettings.clientlower) { $script:clientname = $clientname.ToLower() }
	}
}

function XymonProcsCPUUtilisation
{
	# XymonProcsCpu is a table with 4 elements:
	# 	0 = process handle
	# 	1 = last tick value
	# 	2 = ticks used since last poll
	# 	3 = activeflag

	if ((get-variable -erroraction SilentlyContinue "XymonProcsCpu") -eq $null) {
		$script:XymonProcsCpu = @{ 0 = ( $null, 0, 0, $false) }
		$script:XymonProcsCpuTStart = (Get-Date).ticks
		$script:XymonProcsCpuElapsed = 0
	}
	else {
		$script:XymonProcsCpuElapsed = (Get-Date).ticks - $script:XymonProcsCpuTStart
		$script:XymonProcsCpuTStart = (Get-Date).Ticks
	}
	
	$allprocs = Get-Process
	foreach ($p in $allprocs) {
		$thisp = $script:XymonProcsCpu[$p.Id]
		if ($thisp -eq $null -and $p.Id -ne 0) {
			# New process - create an entry in the curprocs table
			# We use static values here, because some get-process entries have null values
			# for the tick-count (The "SYSTEM" and "Idle" processes).
			$script:XymonProcsCpu += @{ $p.Id = @($null, 0, 0, $false) }
			$thisp = $script:XymonProcsCpu[$p.Id]
		}

		$thisp[3] = $true
		$thisp[2] = $p.TotalProcessorTime.Ticks - $thisp[1]
		$thisp[1] = $p.TotalProcessorTime.Ticks
		$thisp[0] = $p
	}
}


function XymonCollectInfo
{
	$script:cpuinfo = @(Get-WmiObject -Class Win32_Processor)
	$script:totalload = 0
	$script:numcpus  = $cpuinfo.Count
	$script:numcores = 0
	$script:numvcpus = 0
	foreach ($cpu in $cpuinfo) { 
		$script:totalload += $cpu.LoadPercentage
		$script:numcores += $cpu.NumberOfCores
		$script:numvcpus += $cpu.NumberOfLogicalProcessors
	}
	$script:totalload /= $numcpus

	$script:osinfo = Get-WmiObject -Class Win32_OperatingSystem
	$script:svcs = Get-WmiObject -Class Win32_Service | Sort-Object -Property Name
	$script:procs = Get-Process | Sort-Object -Property Id
	$mydisks = @()
	foreach ($disktype in $wanteddisks) { 
		$mydisks += @( (Get-WmiObject -Class Win32_LogicalDisk | where { $_.DriveType -eq $disktype } ))
	}
	$script:disks = $mydisks | Sort-Object DeviceID
	$script:netifs = Get-WmiObject -Class Win32_NetworkAdapterConfiguration | where { $_.IPEnabled -eq $true }

	$script:svcprocs = @{([int]-1) = ""}
	foreach ($s in $svcs) {
		if ($s.State -eq "Running") {
			if ($svcprocs[([int]$s.ProcessId)] -eq $null) {
				$script:svcprocs += @{ ([int]$s.ProcessId) = $s.Name }
			}
			else {
				$script:svcprocs[([int]$s.ProcessId)] += ("/" + $s.Name)
			}
		}
	}
	
	$script:localdatetime = $osinfo.ConvertToDateTime($osinfo.LocalDateTime)
	$script:uptime = $localdatetime - $osinfo.ConvertToDateTime($osinfo.LastBootUpTime)

	$script:usercount = 0	# FIXME

	XymonProcsCPUUtilisation
}

function WMIProp($class)
{
	$wmidata = Get-WmiObject -Class $class
	$props = ($wmidata | Get-Member -MemberType Property | Sort-Object -Property Name | where { $_.Name -notlike "__*" })
	foreach ($p in $props) {
		$p.Name + " : " + $wmidata.($p.Name)
	}
}

function UnixDate([System.DateTime] $t)
{
	$t.ToString("ddd dd MMM HH:mm:ss yyyy")
}

function epochTime([System.DateTime] $t)
{
		[uint32](($t.Ticks - ([DateTime] "1/1/1970 00:00:00").Ticks) / 10000000) - $osinfo.CurrentTimeZone*60

}

function filesize($file,$clsize=4KB)
{
    return [math]::floor((($_.Length -1)/$clsize + 1) * $clsize/1KB)
}

function du([string]$dir,[int]$clsize=0)
{
    if($clsize -eq 0) {
        $drive = "{0}:" -f [string](get-item $dir | %{ $_.psdrive })
        $clsize = [int](Get-WmiObject win32_Volume | ? { $_.DriveLetter -eq $drive }).BlockSize
        if($clsize -eq 0 -or $clsize -eq $null) { $clsize = 4096 } # default in case not found
    }
    $sum = 0
    $dulist = ""
    get-childitem $dir -Force | % {
        if( $_.Attributes -like "*Directory*" ) {
           $dulist += du ("{0}\{1}" -f [string]$dir,$_.Name) $clsize | out-string
           $sum += $dulist.Split("`n")[-2].Split("`t")[0] # get size for subdir
        } else { 
           $sum += filesize $_ $clsize
        }
    }
    "$dulist$sum`t$dir"
}


function XymonPrintProcess($pobj, $name, $pct)
{
	$pcpu = (("{0:F1}" -f $pct) + "`%").PadRight(8)
	$ppid = ([string]($pobj[0]).Id).PadRight(6)
	
	if ($name.length -gt 30) { $name = $name.substring(0, 30) }
	$pname = $name.PadRight(32)

	$pprio = ([string]$pobj[0].BasePriority).PadRight(5)
	$ptime = (([string]$pobj[0].TotalProcessorTime).Split(".")[0]).PadRight(9)
	$pmem = ([string]($pobj[0].WorkingSet64 / 1KB)) + "k"

	$pcpu + $ppid + $pname + $pprio + $ptime + $pmem
}

function XymonDate
{
	"[date]"
	UnixDate $localdatetime
}

function XymonClock
{
	$epoch = epochTime $localdatetime

	"[clock]"
	"epoch: " + $epoch
	"local: " + (UnixDate $localdatetime)
	"UTC: " + (UnixDate $localdatetime.AddMinutes(-$osinfo.CurrentTimeZone))
	$timesource = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\Parameters').Type
	"Time Synchronisation type: " + $timesource
	if ($timesource -eq "NTP") {
		"NTP server: " + (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\Parameters').NtpServer
	}
	$w32qs = w32tm /query /status  # will not run on 2003, XP or earlier
	if($?) { $w32qs }
}

function XymonUptime
{
	"[uptime]"
	"sec: " + [string] ([int]($uptime.Ticks / 10000000))
	([string]$uptime.Days) + " days " + ([string]$uptime.Hours) + " hours " + ([string]$uptime.Minutes) + " minutes " + ([string]$uptime.Seconds) + " seconds"
	"Bootup: " + $osinfo.LastBootUpTime
}

function XymonUname
{
	"[uname]"
	$osinfo.Caption + " " + $osinfo.CSDVersion + " (build " + $osinfo.BuildNumber + ")"
}

function XymonCpu
{
	"[cpu]"
	"up: " + ([string]$uptime.Days) + " days, " + $usercount + " users, " + $procs.count + " procs, load=" + ([string]$totalload) + "`%"
	""
	"CPU states:"
	"`ttotal`t" + ([string]$totalload) + "`%"
	foreach ($cpu in $cpuinfo) { 
		"`t" + $cpu.DeviceID + "`t" + $cpu.LoadPercentage + "`%"
	}

	if ($script:XymonProcsCpuElapsed -gt 0) {
		""
		"CPU".PadRight(8) + "PID".PadRight(6) + "Image Name".PadRight(32) + "Pri".PadRight(5) + "Time".PadRight(9) + "MemUsage"

		foreach ($p in $script:XymonProcsCpu.Keys) {
			$thisp = $script:XymonProcsCpu[$p]
			if ($thisp[3] -eq $true) {
				# Process found in the latest Get-Procs call, so it is active.
				if ($svcprocs[$p] -eq $null) {
					$pname = ($thisp[0]).Name
				}
				else {
					$pname = "SVC:" + $svcprocs[$p]
				}

				$usedpct = ([int](10000*($thisp[2] / $script:XymonProcsCpuElapsed))) / 100
				XymonPrintProcess $thisp $pname $usedpct

				$thisp[3] = $false	# Set flag to catch a dead process on the next run
			}
			else {
				# Process has died, clear it.
				$thisp[2] = $thisp[1] = 0
				$thisp[0] = $null
			}
		}
	}
}

function XymonDisk
{
	"[disk]"
	"{0,-15} {1,9} {2,9} {3,9} {4,9} {5,10} {6}" -f "Filesystem", "1K-blocks", "Used", "Avail", "Capacity", "Mounted", "Summary(Total\Avail GB)"
	foreach ($d in $disks) {
		$diskletter = ($d.DeviceId).Trim(":")
		[uint32]$diskusedKB = ([uint32]($d.Size/1KB)) - ([uint32]($d.FreeSpace/1KB))	# PS ver 1 doesnt support subtraction uint64's
		[uint32]$disksizeKB = [uint32]($d.size/1KB)

		$dsKB = "{0:F0}" -f ($d.Size / 1KB); $dsGB = "{0:F2}" -f ($d.Size / 1GB)
		$duKB = "{0:F0}" -f ($diskusedKB); $duGB = "{0:F2}" -f ($diskusedKB / 1KB);
		$dfKB = "{0:F0}" -f ($d.FreeSpace / 1KB); $dfGB = "{0:F2}" -f ($d.FreeSpace / 1GB)
		
		if ($d.Size -gt 0) {
			$duPCT = $diskusedKB/$disksizeKB
		}
		else {
			$duPCT = 0
		}

		"{0,-15} {1,9} {2,9} {3,9} {4,9:0%} {5,10} {6}" -f $diskletter, $dsKB, $duKB, $dfKB, $duPCT, "/FIXED/$diskletter", $dsGB + "\" + $dfGB
	}
}

function XymonMemory
{
	$physused  = [int](($osinfo.TotalVisibleMemorySize - $osinfo.FreePhysicalMemory)/1KB)
	$phystotal = [int]($osinfo.TotalVisibleMemorySize / 1KB)
	$pageused  = [int](($osinfo.SizeStoredInPagingFiles - $osinfo.FreeSpaceInPagingFiles) / 1KB)
	$pagetotal = [int]($osinfo.SizeStoredInPagingFiles / 1KB)
	$virtused  = [int](($osinfo.TotalVirtualMemorySize - $osinfo.FreeVirtualMemory) / 1KB)
	$virttotal = [int]($osinfo.TotalVirtualMemorySize / 1KB)

	"[memory]"
	"memory    Total    Used"
	"physical: $phystotal $physused"
	if($XymonSettings.clientbbwinmembug -eq 0) {  	# 0 = report correctly, 1 = page and virtual switched
		"virtual: $virttotal $virtused"
		"page: $pagetotal $pageused"
	} else {
		"virtual: $pagetotal $pageused"
		"page: $virttotal $virtused"
	}
}

function XymonMsgs
{
	$since = (Get-Date).AddMinutes(-($XymonSettings.maxlogage))
	if ($wantedlogs -eq $null) {
		$wantedlogs = "Application", "System", "Security"
	}

	foreach ($l in $wantedlogs) {
		$log = Get-EventLog -List | where { $_.Log -eq $l }

		$logentries = Get-EventLog -ErrorAction:SilentlyContinue -LogName $log.Log -asBaseObject -After $since | where {$_.EntryType -match "Error|Warning"}
	
		"[msgs:eventlog_$l]"
		if ($logentries -ne $null) {
			foreach ($entry in $logentries) {
				[string]$entry.EntryType + " - " + [string]$entry.TimeGenerated + " - " + [string]$entry.Source + " - " + [string]$entry.Message
			}
		}
	}
}

function ResolveEnvPath($envpath)
{
	$s = $envpath
	while($s -match '%([\w_]+)%') {
		if(! (test-path "env:$($matches[1])")) { return $envpath }
		$s = $s.Replace($matches[0],$(Invoke-Expression "`$env:$($matches[1])"))
	}
	if(! (test-path $s)) { return $envpath }
	resolve-path $s
}

function XymonDir
{
	$script:clientlocalcfg | ? { $_ -match "^dir:(.*)" } | % {
		resolveEnvPath $matches[1] | %{
			"[dir:$($_)]"
			if(test-path $_ -PathType Container) { du $_ }
			elseif(test-path $_) {"ERROR: The path specified is not a directory." }
			else { "ERROR: The system cannot find the path specified." }
		}
	}
}

function XymonFileStat($file,$hash="")
{
    # don't implement hashing yet - don't even check for it...
	if(test-path $_) {
		$fh = get-item $_
		if(test-path $_ -PathType Leaf) {
			"type:100000 (file)"
		} else {
			"type:40000 (directory)"
		}
		"mode:{0} (not implemented)" -f $(if($fh.IsReadOnly) {555} else {777})
		"linkcount:1"
		"owner:0 ({0})" -f $fh.GetAccessControl().Owner
		"group:0 ({0})" -f $fh.GetAccessControl().Group
		if(test-path $_ -PathType Leaf) { "size:{0}" -f $fh.length }
		"atime:{0} ({1})" -f (epochTime $fh.LastAccessTimeUtc),$fh.LastAccessTime.ToString("yyyy/MM/dd-HH:mm:ss")
		"ctime:{0} ({1})" -f (epochTime $fh.CreationTimeUtc),$fh.CreationTime.ToString("yyyy/MM/dd-HH:mm:ss")
		"mtime:{0} ({1})" -f (epochTime $fh.LastWriteTimeUtc),$fh.LastWriteTime.ToString("yyyy/MM/dd-HH:mm:ss")
		if(test-path $_ -PathType Leaf) {
			"FileVersion:{0}" -f $fh.VersionInfo.FileVersion
			"FileDescription:{0}" -f $fh.VersionInfo.FileDescription
		}
	} else {
		"ERROR: The system cannot find the path specified."
	}
}

function XymonFileCheck
{
    # don't implement hashing yet - don't even check for it...
    $script:clientlocalcfg | ? { $_ -match "^file:(.*)$" } | % {
		resolveEnvPath $matches[1] | %{
			"[file:$_]"
			XymonFileStat $_
		}
	}
}

function XymonLogCheck
{
    $script:clientlocalcfg | ? { $_ -match "^log:(.*):(\d+)$" } | % {
		$sizemax=$matches[2]
		resolveEnvPath $matches[1] | %{
			"[logfile:$_]"
			XymonFileStat $_
			"[msgs:$_]"
			XymonLogCheckFile $_ $sizemax
		}
	}
}

function XymonLogCheckFile([string]$file,$sizemax=0)
{
    $f = [system.io.file]::Open($file,"Open","Read","ReadWrite")
    $s = get-item $file
    $nowpos = $f.length
	$savepos = 0
	if($script:logfilepos.$($file) -ne $null) { $savepos = $script:logfilepos.$($file) }
	if($nowpos -lt $savepos) {$savepos = 0} # log file rolled over??
    #"Save: {0}  Len: {1} Diff: {2} Max: {3} Write: {4}" -f $savepos,$nowpos, ($nowpos-$savepos),$sizemax,$s.LastWriteTime
    if($nowpos -gt $savepos) { # must be some more content to check
		$s = new-object system.io.StreamReader($f,$true)
		$dummy = $s.readline()
		$enc = $s.currentEncoding
		$charsize = 1
		if($enc.EncodingName -eq "Unicode") { $charsize = 2 }
		if($nowpos-$savepos -gt $charsize*$sizemax) {$savepos = $nowpos-$charsize*$sizemax}
        $seek = $f.Seek($savepos,0)
		$t = new-object system.io.StreamReader($f,$enc)
		$buf = $t.readtoend()
		if($buf -ne $null) { $buf }
		#"Save2: {0}  Pos: {1} Blen: {2} Len: {3} Enc($charsize): {4}" -f $savepos,$f.Position,$buf.length,$nowpos,$enc.EncodingName
    }
	$script:logfilepos.$($file) = $nowpos # save for next loop
}

function XymonPorts
{
	"[ports]"
	netstat -an
}

function XymonIpconfig
{
	"[ipconfig]"
	ipconfig /all | %{ $_.split("`n") } | ?{ $_ -match "\S" }  # for some reason adds blankline between each line
}

function XymonRoute
{
	"[route]"
	netstat -rn
}

function XymonNetstat
{
	"[netstat]"
	$pref=""
	netstat -s | ?{$_ -match "=|(\w+) Statistics for"} | %{ if($_.contains("=")) {("$pref$_").REPLACE(" ","")}else{$pref=$matches[1].ToLower();""}}
}

function XymonIfstat
{
	"[ifstat]"
    [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() | ?{$_.OperationalStatus -eq "Up"} | %{
        $ad = $_.GetIPv4Statistics() | select BytesSent, BytesReceived
        $ip = $_.GetIPProperties().UnicastAddresses | select Address
		# some interfaces have multiple IPs, so iterate over them reporting same stats
        $ip | %{ "{0} {1} {2}" -f $_.Address.IPAddressToString,$ad.BytesReceived,$ad.BytesSent }
    }
}

function XymonSvcs
{
	"[svcs]"
	"Name".PadRight(39) + " " + "StartupType".PadRight(12) + " " + "Status".PadRight(14) + " " + "DisplayName"
	foreach ($s in $svcs) {
		if ($s.StartMode -eq "Auto") { $stm = "automatic" } else { $stm = $s.StartMode.ToLower() }
		if ($s.State -eq "Running")  { $state = "started" } else { $state = $s.State.ToLower() }
		$s.Name.Replace(" ","_").PadRight(39) + " " + $stm.PadRight(12) + " " + $state.PadRight(14) + " " + $s.DisplayName
	}
}

function XymonProcs
{
	"[procs]"
	"{0,8} {1,-35} {2,-17} {3,-17} {4,-17} {5,8} {6,-7} {7,5} {8}" -f "PID", "User", "WorkingSet/Peak", "VirtualMem/Peak", "PagedMem/Peak", "NPS", "Handles", "%CPU", "Name"
	foreach ($p in $procs) {
		if ($svcprocs[($p.Id)] -ne $null) {
			$procname = "Service:" + $svcprocs[($p.Id)]
		}
		else {
			$procname = $p.Name
		}

		$thiswmip = Get-WmiObject -Class Win32_Process | where { $_.ProcessId -eq $p.Id }
		if(	$thiswmip -ne $null ) { # short-lived process could possibly be gone
			$saveerractpref = $ErrorActionPreference
            $ErrorActionPreference = "SilentlyContinue"
            $owner = ($thiswmip.getowner()).Domain + "\" + ($thiswmip.GetOwner().user)
            $ErrorActionPreference = $saveerractpref
		} else { $owner = "NA" }
		if ($owner -eq "\") { $owner = "SYSTEM" }
		if ($owner.length -gt 32) { $owner = $owner.substring(0, 32) }

		$pws     = "{0,8:F0}/{1,-8:F0}" -f ($p.WorkingSet64 / 1KB), ($p.PeakWorkingSet64 / 1KB)
		$pvmem   = "{0,8:F0}/{1,-8:F0}" -f ($p.VirtualMemorySize64 / 1KB), ($p.PeakVirtualMemorySize64 / 1KB)
		$ppgmem  = "{0,8:F0}/{1,-8:F0}" -f ($p.PagedMemorySize64 / 1KB), ($p.PeakPagedMemorySize64 / 1KB)
		$pnpgmem = "{0,8:F0}" -f ($p.NonPagedSystemMemorySize64 / 1KB)
			
		$thisp = $script:XymonProcsCpu[$p.Id]
		if ($script:XymonProcsCpuElapsed -gt 0 -and $thisp -ne $null) {
			$pcpu = "{0,5:F0}" -f (([int](10000*($thisp[2] / $script:XymonProcsCpuElapsed))) / 100)
		} else {
			$pcpu = "{0,5}" -f "-"
		}

		"{0,8} {1,-35} {2} {3} {4} {5} {6,7:F0} {7} {8}" -f $p.Id, $owner, $pws, $pvmem, $ppgmem, $pnpgmem, $p.HandleCount, $pcpu, $procname
	}
}

function XymonWho
{
	if( $HaveCmd.qwinsta) {
		"[who]"
		qwinsta.exe /counter
	}
}

function XymonUsers
{
	if( $HaveCmd.query) {
		"[users]"
		query user
	}
}

function XymonIISSites
{
    $objSites = [adsi]("IIS://localhost/W3SVC")
	if($objSites.path -ne $null) {
		"[iis_sites]"
		foreach ($objChild in $objSites.Psbase.children | where {$_.KeyType -eq "IIsWebServer"} ) {
			""
			$objChild.servercomment
			$objChild.path
			if($objChild.path -match "\/W3SVC\/(\d+)") { "SiteID: "+$matches[1] }
			foreach ($prop in @("LogFileDirectory","LogFileLocaltimeRollover","LogFileTruncateSize","ServerAutoStart","ServerBindings","ServerState","SecureBindings" )) {
				if( $($objChild | gm -Name $prop ) -ne $null) {
					"{0} {1}" -f $prop,$objChild.$prop.ToString()
				}
			}
		}
	}
}

function XymonWMIOperatingSystem
{
	"[WMI:Win32_OperatingSystem]"
	WMIProp Win32_OperatingSystem
}

function XymonWMIQuickFixEngineering
{
	"[WMI:Win32_QuickFixEngineering]"
	Get-WmiObject -Class Win32_QuickFixEngineering | where { $_.Description -ne "" } | Sort-Object HotFixID | Format-Wide -Property HotFixID -AutoSize
}

function XymonWMIProduct
{
    # run as job, since Win32_Product WMI dies on some systems (e.g. XP)
	$job = Get-WmiObject -Class Win32_Product -AsJob | wait-job
	if($job.State -eq "Completed") {
		"[WMI:Win32_Product]"
		$fmt = "{0,-70} {1,-15} {2}"
		$fmt -f "Name", "Version", "Vendor"
		$fmt -f "----", "-------", "------"
		receive-job $job | Sort-Object Name | 
		foreach {
			$fmt -f $_.Name, $_.Version, $_.Vendor
		}
	}
	remove-job $job
}

function XymonWMIComputerSystem
{
	"[WMI:Win32_ComputerSystem]"
	WMIProp Win32_ComputerSystem
}

function XymonWMIBIOS
{
	"[WMI:Win32_BIOS]"
	WMIProp Win32_BIOS
}

function XymonWMIProcessor
{
	"[WMI:Win32_Processor]"
	$cpuinfo | Format-List DeviceId,Name,CurrentClockSpeed,NumberOfCores,NumberOfLogicalProcessors,CpuStatus,Status,LoadPercentage
}

function XymonWMIMemory
{
	"[WMI:Win32_PhysicalMemory]"
	Get-WmiObject -Class Win32_PhysicalMemory | Format-Table -AutoSize BankLabel,Capacity,DataWidth,DeviceLocator
}

function XymonWMILogicalDisk
{
	"[WMI:Win32_LogicalDisk]"
	Get-WmiObject -Class Win32_LogicalDisk | Format-Table -AutoSize
}

function XymonEventLogs
{
	"[EventlogSummary]"
	Get-EventLog -List | Format-Table -AutoSize
}


function XymonSend($msg, $servers)
{
	$saveresponse = 1	# Only on the first server
	$outputbuffer = ""
	$ASCIIEncoder = New-Object System.Text.ASCIIEncoding

	foreach ($srv in $servers) {
		$srvparams = $srv.Split(":")
		# allow for server names that may resolve to multiple A records
		$srvIPs = $srvparams[0] | %{[system.net.dns]::GetHostAddresses($_)} | %{ $_.IPAddressToString}
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

	$outputbuffer
}

function XymonClientConfig($cfglines)
{
	if ($cfglines -eq $null -or $cfglines -eq "") { return }

	# Convert to Windows-style linebreaks
	$script:clientlocalcfg = $cfglines.Split("`n")
	$clientlocalcfg >$XymonSettings.clientconfigfile

	# Source the new config
	if ($XymonSettings.clientremotecfgexec -ne 0 -and (test-path -PathType Leaf $XymonSettings.clientconfigfile) ) { . $XymonSettings.clientconfigfile }
}

function XymonReportConfig
{
	"[XymonConfig]"
	"XymonSettings"
	$XymonSettings
	""
	"HaveCmd"
	$HaveCmd
	foreach($v in @("wanteddisks", "wantedlogs", "XymonClientVersion", "clientname" )) {
		""; "$v"
		Invoke-Expression "`$$v"
	}
}

function XymonClientSections {
	XymonUname
	XymonCpu
	XymonDisk
	XymonMemory
	XymonEventLogs
	XymonMsgs
	XymonProcs
	XymonNetstat
	XymonPorts
	XymonIPConfig
	XymonRoute
	XymonIfstat
	XymonSvcs
	XymonDir
	XymonFileCheck
	XymonLogCheck
	XymonUptime
	XymonWho
	XymonUsers
	XymonIISSites

	XymonWMIOperatingSystem
	XymonWMIComputerSystem
	XymonWMIBIOS
	XymonWMIProcessor
	XymonWMIMemory
	XymonWMILogicalDisk

	$XymonWMIQuickFixEngineeringCache
	$XymonWMIProductCache

	XymonReportConfig
}

function XymonClientInstall([string]$scriptname)
{
	if((Get-Service -ea:SilentlyContinue $xymonsvcname) -eq $null) {
		$newsvc = New-Service $xymonsvcname $xymondir\XymonPSClient.exe -DisplayName "Xymon Powershell Client" -StartupType Automatic -Description "Reports to Xymon monitoring server client data gathered by powershell script"
	}
	if((Get-Item -ea:SilentlyContinue HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters) -eq $null) {
		$newitm = New-Item HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters
	}
	Set-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters Application "$PSHOME\powershell.exe"
	Set-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters "Application Parameters" "-nonInteractive -ExecutionPolicy Unrestricted -File $scriptname"
	Set-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters "Application Default" $xymondir
}
##### Main code #####
XymonInit
$ret = 0
# check for install/start/stop for service management
if($args -eq "Install") {
	XymonClientInstall (Resolve-Path $MyInvocation.InvocationName)
	$ret=1
}
if($args -eq "Start") {
	if((get-service $xymonsvcname).Status -ne "Running") { start-service $xymonsvcname }
	$ret=1
}
if($args -eq "Stop") {
	if((get-service $xymonsvcname).Status -eq "Running") { stop-service $xymonsvcname }
	$ret=1
}
if($ret) {return}

# assume no other args, so run as normal

$running = $true
$loopcount = ($XymonSettings.slowscanrate - 1)

while ($running -eq $true) {
	$starttime = Get-Date
	
	$loopcount++; 
	if ($loopcount -eq $XymonSettings.slowscanrate) { 
		$loopcount = 0
		$XymonWMIQuickFixEngineeringCache = XymonWMIQuickFixEngineering
		$XymonWMIProductCache = XymonWMIProduct
		[GC]::Collect()
	}

	XymonCollectInfo

	$clout = "client " + $clientname + ".bbwin win32" | Out-String
	$clsecs = XymonClientSections | Out-String
	$localdatetime = Get-Date
	$clout += XymonDate | Out-String
	$clout += XymonClock | Out-String
	$clout +=  $clsecs
	
	Get-Date >> $XymonSettings.clientlogfile
	XymonReportConfig >> $XymonSettings.clientlogfile
	$newconfig = XymonSend $clout $XymonSettings.servers
	XymonClientConfig $newconfig
	$delay = ($XymonSettings.loopinterval - (Get-Date).Subtract($starttime).TotalSeconds)
	if ($delay -gt 0) { sleep $delay }
}
