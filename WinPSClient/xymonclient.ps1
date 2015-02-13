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
$xymondir = split-path -parent $MyInvocation.MyCommand.Definition

# -----------------------------------------------------------------------------------

$Version = "1.4"
$XymonClientVersion = "${Id}: xymonclient.ps1  $Version 2014-09-15 zak.beck@accenture.com"
# detect if we're running as 64 or 32 bit
$XymonRegKey = $(if([System.IntPtr]::Size -eq 8) { "HKLM:\SOFTWARE\Wow6432Node\XymonPSClient" } else { "HKLM:\SOFTWARE\XymonPSClient" })
$XymonClientCfg = join-path $xymondir 'xymonclient_config.xml'
$ServiceChecks = @{}

function SetIfNot($obj,$key,$value)
{
    if($obj.$key -eq $null) { $obj | Add-Member -MemberType noteproperty -Name $key -Value $value }
}

function XymonConfig
{
    if (Test-Path $XymonClientCfg)
    {
        XymonInitXML
        $script:XymonCfgLocation = "XML: $XymonClientCfg"
    }
    else
    {
        XymonInitRegistry
        $script:XymonCfgLocation = "Registry"
    }
    XymonInit
}

function XymonInitXML
{
    $xmlconfig = [xml](Get-Content $XymonClientCfg)
    $script:XymonSettings = $xmlconfig.XymonSettings
}

function XymonInitRegistry
{
    $script:XymonSettings = Get-ItemProperty -ErrorAction:SilentlyContinue $XymonRegKey
}

function XymonInit
{
	if($script:XymonSettings -eq $null) {
		$script:XymonSettings = New-Object Object
	} else {
		# any special handling for settings from reg keys
		if($script:XymonSettings.servers -match " ") {
			$script:XymonSettings.servers = $script:XymonSettings.servers.Split(" ")
		}
		if($script:XymonSettings.wanteddisks -match " ") {
			$script:XymonSettings.wanteddisks = $script:XymonSettings.wanteddisks.Split(" ")
		}
	}
	SetIfNot $script:XymonSettings servers $xymonservers # List your Xymon servers here
	# SetIfNot $script:XymonSettings clientname "winxptest"	# Define this to override the default client hostname

	# Params for default clientname
	SetIfNot $script:XymonSettings clientfqdn 1 # 0 = unqualified, 1 = fully-qualified
	SetIfNot $script:XymonSettings clientlower 1 # 0 = unqualified, 1 = fully-qualified
    
	if ($script:XymonSettings.clientname -eq $null -or $script:XymonSettings.clientname -eq "") { # set name based on rules
		$ipProperties = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
		$clname  = $ipProperties.HostName
		if ($script:XymonSettings.clientfqdn -eq 1 -and ($ipProperties.DomainName -ne $null)) { 
			$clname += "." + $ipProperties.DomainName
		}
		if ($script:XymonSettings.clientlower -eq 1) { $clname = $clname.ToLower() }
		SetIfNot $script:XymonSettings clientname $clname
        $script:clientname = $clname
	}
    else
    {
        $script:clientname = $script:XymonSettings.clientname
    }

	# Params for various client options
	SetIfNot $script:XymonSettings clientbbwinmembug 1 # 0 = report correctly, 1 = page and virtual switched
	SetIfNot $script:XymonSettings clientremotecfgexec 0 # 0 = don't run remote config, 1 = run remote config
	SetIfNot $script:XymonSettings clientconfigfile "$env:TEMP\xymonconfig.cfg" # path for saved client-local.cfg section from server
	SetIfNot $script:XymonSettings clientlogfile "$env:TEMP\xymonclient.log" # path for logfile
	SetIfNot $script:XymonSettings loopinterval 300 # seconds to repeat client reporting loop
	SetIfNot $script:XymonSettings maxlogage 60 # minutes age for event log reporting
	SetIfNot $script:XymonSettings slowscanrate 72 # repeats of main loop before collecting slowly changing information again
	SetIfNot $script:XymonSettings reportevt 1 # scan eventlog and report (can be very slow)
	SetIfNot $script:XymonSettings wanteddisks @( 3 )	# 3=Local disks, 4=Network shares, 2=USB, 5=CD
    SetIfNot $script:XymonSettings EnableWin32_Product 0 # 0 = do not use Win32_product, 1 = do
                        # see http://support.microsoft.com/kb/974524 for reasons why Win32_Product is not recommended!
    SetIfNot $script:XymonSettings EnableWin32_QuickFixEngineering 0 # 0 = do not use Win32_QuickFixEngineering, 1 = do
    SetIfNot $script:XymonSettings EnableWMISections 0 # 0 = do not produce [WMI: sections (OS, BIOS, Processor, Memory, Disk), 1 = do
    SetIfNot $script:XymonSettings ClientProcessPriority 'High' # possible values Normal, Idle, High, RealTime, Belo wNormal, AboveNormal


    SetIfNot $script:XymonSettings servergiflocation '/xymon/gifs/'
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

function UserSessionCount
{
	$q = get-wmiobject win32_logonsession | %{ $_.logonid}
	$s = 0
	get-wmiobject win32_session | ?{ 2,10 -eq $_.LogonType} | ?{$q -eq $_.logonid} | %{
		$z = $_.logonid
		get-wmiobject win32_sessionprocess | ?{ $_.Antecedent -like "*LogonId=`"$z`"*" } | %{
			if($_.Dependent -match "Handle=`"(\d+)`"") {
				get-wmiobject win32_process -filter "processid='$($matches[1])'" }
		} | select -first 1 | %{ $s++ }
	}
	$s
}

function XymonCollectInfo
{
    WriteLog "Executing XymonCollectInfo"
    WriteLog "XymonCollectInfo: CPU info (WMI)"
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

    WriteLog "XymonCollectInfo: OS info (including memory) (WMI)"
	$script:osinfo = Get-WmiObject -Class Win32_OperatingSystem
    WriteLog "XymonCollectInfo: Service info (WMI)"
	$script:svcs = Get-WmiObject -Class Win32_Service | Sort-Object -Property Name
    WriteLog "XymonCollectInfo: Process info"
	$script:procs = Get-Process | Sort-Object -Property Id
    WriteLog "XymonCollectInfo: Disk info (WMI)"
	$mydisks = @()
	foreach ($disktype in $script:XymonSettings.wanteddisks) { 
		$mydisks += @( (Get-WmiObject -Class Win32_LogicalDisk | where { $_.DriveType -eq $disktype } ))
	}
	$script:disks = $mydisks | Sort-Object DeviceID
<<<<<<< HEAD
    WriteLog "XymonCollectInfo: Network adapter info (WMI)"
	$script:netifs = Get-WmiObject -Class Win32_NetworkAdapterConfiguration | where { $_.IPEnabled -eq $true }
=======

    # netifs does not appear to be used, commented out
	#$script:netifs = Get-WmiObject -Class Win32_NetworkAdapterConfiguration | where { $_.IPEnabled -eq $true }
>>>>>>> no-priority

    WriteLog "XymonCollectInfo: Building table of service processes (uses WMI data)"
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
	
    WriteLog "XymonCollectInfo: Date processing (uses WMI data)"
	$script:localdatetime = $osinfo.ConvertToDateTime($osinfo.LocalDateTime)
	$script:uptime = $localdatetime - $osinfo.ConvertToDateTime($osinfo.LastBootUpTime)

	$script:usercount = UserSessionCount

    WriteLog "XymonCollectInfo: calling XymonProcsCPUUtilisation"
	XymonProcsCPUUtilisation
    WriteLog "XymonCollectInfo finished"
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

function epochTimeUtc([System.DateTime] $t)
{
		[uint32](($t.Ticks - ([DateTime] "1/1/1970 00:00:00").Ticks) / 10000000)

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
	"UTC: " + (UnixDate $localdatetime.ToUniversalTime())
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

function XymonClientVersion
{
    "[clientversion]"
    $Version
}

function XymonCpu
{
    WriteLog "XymonCpu start"

	"[cpu]"
	"up: {0} days, {1} users, {2} procs, load={3}%" -f [string]$uptime.Days, $usercount, $procs.count, [string]$totalload
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
    WriteLog "XymonCpu finished."
}

function XymonDisk
{
    WriteLog "XymonDisk start"
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
    WriteLog "XymonDisk finished."
}

function XymonMemory
{
    WriteLog "XymonMemory start"
	$physused  = [int](($osinfo.TotalVisibleMemorySize - $osinfo.FreePhysicalMemory)/1KB)
	$phystotal = [int]($osinfo.TotalVisibleMemorySize / 1KB)
	$pageused  = [int](($osinfo.SizeStoredInPagingFiles - $osinfo.FreeSpaceInPagingFiles) / 1KB)
	$pagetotal = [int]($osinfo.SizeStoredInPagingFiles / 1KB)
	$virtused  = [int](($osinfo.TotalVirtualMemorySize - $osinfo.FreeVirtualMemory) / 1KB)
	$virttotal = [int]($osinfo.TotalVirtualMemorySize / 1KB)

	"[memory]"
	"memory    Total    Used"
	"physical: $phystotal $physused"
	if($script:XymonSettings.clientbbwinmembug -eq 0) {  	# 0 = report correctly, 1 = page and virtual switched
		"virtual: $virttotal $virtused"
		"page: $pagetotal $pageused"
	} else {
		"virtual: $pagetotal $pageused"
		"page: $virttotal $virtused"
	}
    WriteLog "XymonMemory finished."
}

function XymonMsgs
{
	if($script:XymonSettings.reportevt -eq 0) {return}
	$since = (Get-Date).AddMinutes(-($script:XymonSettings.maxlogage))


    # default logs - may be overridden by config
    $wantedlogs = "Application", "System", "Security"
    $maxpayloadlength = 1024
    $payload = ''

    # this function no longer uses $script:XymonSettings.wantedlogs
    # - it now uses eventlogswanted from the remote config
    if ($script:clientlocalcfg_entries.keys | where { $_ -match '^eventlogswanted:(.+):(\d+)$' })
    {
        $wantedlogs = $matches[1] -split ','
        $maxpayloadlength = $matches[2]
    }

    WriteLog "Event Log processing - max payload: $maxpayloadlength - wanted logs: $wantedlogs"

	foreach ($l in $wantedlogs) 
    {
        # only scan the current log if there is space in the payload
        if ($payload.Length -lt $maxpayloadlength)
        {
            WriteLog "Processing event log $l"

            $log = Get-EventLog -List | where { $_.Log -eq $l }

            $logentries = @(Get-EventLog -ErrorAction:SilentlyContinue -LogName $log.Log -asBaseObject -After $since)

            WriteLog "Event log $l entries since last scan: $($logentries.Length)"
            
            # filter based on clientlocal.cfg / clientconfig.cfg
            if ($script:clientlocalcfg_entries -ne $null)
            {
                $filterkey = $script:clientlocalcfg_entries.keys | where { $_ -match "^eventlog\:$l" }
                if ($filterkey -ne $null -and $script:clientlocalcfg_entries.ContainsKey($filterkey))
                {
                    WriteLog "Found a configured filter for log $l"
                    $output = @()
                    foreach ($entry in $logentries)
                    {
                        foreach ($filter in $script:clientlocalcfg_entries[$filterkey])
                        {
                            if ($filter -match '^ignore')
                            {
                                $filter = $filter -replace '^ignore ', ''
                                if ($entry.Source -match $filter -or $entry.Message -match $filter)
                                {
                                    $exclude = $true
                                    break
                                }
                            }
                        }
                        if (-not $exclude)
                        {
                            $output += $entry
                        }
                    }
                    $logentries = $output
                }
            }

            if ($logentries -ne $null) 
            {
                WriteLog "Event log $l adding to payload"

                $payload += "[msgs:eventlog_$l]" + [environment]::newline

                foreach ($entry in $logentries) 
                {
                    $payload += [string]$entry.EntryType + " - " +`
                        [string]$entry.TimeGenerated + " - " + `
                        [string]$entry.Source + " - " + `
                        [string]$entry.Message + [environment]::newline
                    
                    if ($payload.Length -gt $maxpayloadlength)
                    {
                        break;
                    }
                }
            }
        }
	}
    WriteLog "Event log processing finished"
    $payload
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
    #$script:clientlocalcfg | ? { $_ -match "^dir:(.*)" } | % {
	$script:clientlocalcfg_entries.keys | where { $_ -match "^dir:(.*)" } |`
        foreach {
		resolveEnvPath $matches[1] | foreach {
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
		"atime:{0} ({1})" -f (epochTimeUtc $fh.LastAccessTimeUtc),$fh.LastAccessTime.ToString("yyyy/MM/dd-HH:mm:ss")
		"ctime:{0} ({1})" -f (epochTimeUtc $fh.CreationTimeUtc),$fh.CreationTime.ToString("yyyy/MM/dd-HH:mm:ss")
		"mtime:{0} ({1})" -f (epochTimeUtc $fh.LastWriteTimeUtc),$fh.LastWriteTime.ToString("yyyy/MM/dd-HH:mm:ss")
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
    #$script:clientlocalcfg | ? { $_ -match "^file:(.*)$" } | % {
    $script:clientlocalcfg_entries.keys | where { $_ -match "^file:(.*)$" } |`
        foreach {
		resolveEnvPath $matches[1] | foreach {
			"[file:$_]"
			XymonFileStat $_
		}
	}
}

function XymonLogCheck
{
    #$script:clientlocalcfg | ? { $_ -match "^log:(.*):(\d+)$" } | % {
    $script:clientlocalcfg_entries.keys | where { $_ -match "^log:(.*):(\d+)$" } |`
        foreach {
		$sizemax=$matches[2]
		resolveEnvPath $matches[1] | foreach {
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
	if($script:logfilepos.$($file) -ne $null) { $savepos = $script:logfilepos.$($file)[0] }
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
	if($script:logfilepos.$($file) -ne $null) {
		$script:logfilepos.$($file) = $script:logfilepos.$($file)[1..6]
	} else {
		$script:logfilepos.$($file) = @(0,0,0,0,0,0) # save for next loop
	}
	$script:logfilepos.$($file) += $nowpos # save for next loop
}

function XymonDirSize
{
    # dirsize:<path>:<gt/lt/eq>:<size bytes>:<fail colour>
    # match number:
    #        :  1   :   2      :     3      :     4
    # <path> may be a simple path (c:\temp) or contain an environment variable
    # e.g. %USERPROFILE%\temp
    WriteLog "Executing XymonDirSize"
    $outputtext = ''
    $groupcolour = 'green'
    $script:clientlocalcfg_entries.keys | where { $_ -match '^dirsize:([a-z%][a-z:][^:]+):([gl]t|eq):(\d+):(.+)$' } |`
        foreach {
            resolveEnvPath $matches[1] | foreach {

                if (test-path $_ -PathType Container)
                {
                    WriteLog "DirSize: $_"
                    # could use "get-childitem ... -recurse | measure ..." here 
                    # but that does not work well when there are many files/subfolders
                    $objFSO = new-object -com Scripting.FileSystemObject
                    $size = $objFSO.GetFolder($_).Size
                    $criteriasize = ($matches[3] -as [int])
                    $conditionmet = $false
                    if ($matches[2] -eq 'gt')
                    {
                        $conditionmet = $size -gt $criteriasize
                        $conditiontype = '>'
                    }
                    elseif ($matches[2] -eq 'lt')
                    {
                        $conditionmet = $size -lt $criteriasize
                        $conditiontype = '<'
                    }
                    else
                    {
                        # eq
                        $conditionmet = $size -eq $criteriasize
                        $conditiontype = '='
                    }
                    if ($conditionmet)
                    {
                        $alertcolour = $matches[4]
                    }
                    else
                    {
                        $alertcolour = 'green'
                    }

                    # report out - 
                    #  {0} = colour (matches[4])
                    #  {1} = folder name
                    #  {2} = folder size
                    #  {3} = condition symbol (<,>,=)
                    #  {4} = alert size
                    $outputtext += (('<img src="{5}{0}.gif" alt="{0}" ' +`
                        'height="16" width="16" border="0">' +`
                        '{1} size is {2} bytes. Alert if {3} {4} bytes.<br>') `
                        -f $alertcolour, $_, $size, $conditiontype, $matches[3], $script:XymonSettings.servergiflocation)
                    # set group colour to colour if it is not already set to a 
                    # higher alert state colour
                    if ($groupcolour -eq 'green' -and $alertcolour -eq 'yellow')
                    {
                        $groupcolour = 'yellow'
                    }
                    elseif ($alertcolour -eq 'red')
                    {
                        $groupcolour = 'red'
                    }
                }
            }
        }

    if ($outputtext -ne '')
    {
        $outputtext = (get-date -format G) + '<br><h2>Directory Size</h2>' + $outputtext
        $output = ('status {0}.dirsize {1} {2}' -f $script:clientname, $groupcolour, $outputtext)
        WriteLog "dirsize: Sending $output"
        XymonSend $output $script:XymonSettings.servers
    }
}

function XymonDirTime
{
    # dirtime:<path>:<unused>:<gt/lt/eq>:<alerttime>:<colour>
    # match number:
    #        :  1   :    2   :     3    :     4     :   5
    # <path> may be a simple path (c:\temp) or contain an environment variable
    # e.g. %USERPROFILE%\temp
    # <alerttime> = number of minutes to alert after
    # e.g. if a directory should be modified every 10 minutes
    # alert for gt 10
    WriteLog "Executing XymonDirTime"
    $outputtext = ''
    $groupcolour = 'green'
    $script:clientlocalcfg_entries.keys | where { $_ -match '^dirtime:([a-z%][a-z:][^:]+):([^:]*):([gl]t|eq):(\d+):(.+)$' } |`
        foreach {
            resolveEnvPath $matches[1] | foreach {

                if (test-path $_ -PathType Container)
                {
                    WriteLog "DirTime: $_"
                    $minutesdiff = ((get-date) - (Get-Item $_).LastWriteTime).TotalMinutes
                    $criteriaminutes = ($matches[4] -as [int])
                    $conditionmet = $false
                    if ($matches[3] -eq 'gt')
                    {
                        $conditionmet = $minutesdiff -gt $criteriaminutes
                        $conditiontype = '>'
                    }
                    elseif ($matches[3] -eq 'lt')
                    {
                        $conditionmet = $minutesdiff -lt $criteriaminutes
                        $conditiontype = '<'
                    }
                    else
                    {
                        $conditionmet = $minutesdiff -eq $criteriaminutes
                        $conditiontype = '='
                    }
                    if ($conditionmet)
                    {
                        $alertcolour = $matches[5]
                    }
                    else
                    {
                        $alertcolour = 'green'
                    }
                    # report out - 
                    #  {0} = colour (matches[5])
                    #  {1} = folder name
                    #  {2} = folder modified x minutes ago
                    #  {3} = condition symbol (<,>,=)
                    #  {4} = alert criteria minutes
                    $outputtext += (('<img src="{5}{0}.gif" alt="{0}"' +`
                        'height="16" width="16" border="0">' +`
                        '{1} updated {2:F1} minutes ago. Alert if {3} {4} minutes ago.<br>') `
                        -f $alertcolour, $_, $minutesdiff, $conditiontype, $criteriaminutes, $script:XymonSettings.servergiflocation)
                    # set group colour to colour if it is not already set to a 
                    # higher alert state colour
                    if ($groupcolour -eq 'green' -and $alertcolour -eq 'yellow')
                    {
                        $groupcolour = 'yellow'
                    }
                    elseif ($alertcolour -eq 'red')
                    {
                        $groupcolour = 'red'
                    }
                }
            }
        }

    if ($outputtext -ne '')
    {
        $outputtext = (get-date -format G) + '<br><h2>Folder Last Modified Time In Minutes</h2>' + $outputtext
        $output = ('status {0}.dirtime {1} {2}' -f $script:clientname, $groupcolour, $outputtext)
        WriteLog "dirtime: Sending $output"
        XymonSend $output $script:XymonSettings.servers
    }
}

function XymonPorts
{
    WriteLog "XymonPorts start"
	"[ports]"
	netstat -an
    WriteLog "XymonPorts finished."
}

function XymonIpconfig
{
    WriteLog "XymonIpconfig start"
	"[ipconfig]"
	ipconfig /all | %{ $_.split("`n") } | ?{ $_ -match "\S" }  # for some reason adds blankline between each line
    WriteLog "XymonIpconfig finished."
}

function XymonRoute
{
    WriteLog "XymonRoute start"
	"[route]"
	netstat -rn
    WriteLog "XymonRoute finished."
}

function XymonNetstat
{
    WriteLog "XymonNetstat start"
	"[netstat]"
	$pref=""
	netstat -s | ?{$_ -match "=|(\w+) Statistics for"} | %{ if($_.contains("=")) {("$pref$_").REPLACE(" ","")}else{$pref=$matches[1].ToLower();""}}
    WriteLog "XymonNetstat finished."
}

function XymonIfstat
{
    WriteLog "XymonIfstat start"
	"[ifstat]"
    [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() | ?{$_.OperationalStatus -eq "Up"} | %{
        $ad = $_.GetIPv4Statistics() | select BytesSent, BytesReceived
        $ip = $_.GetIPProperties().UnicastAddresses | select Address
		# some interfaces have multiple IPs, so iterate over them reporting same stats
        $ip | %{ "{0} {1} {2}" -f $_.Address.IPAddressToString,$ad.BytesReceived,$ad.BytesSent }
    }
    WriteLog "XymonIfstat finished."
}

function XymonSvcs
{
    WriteLog "XymonSvcs start"
    "[svcs]"
    "Name".PadRight(39) + " " + "StartupType".PadRight(12) + " " + "Status".PadRight(14) + " " + "DisplayName"
    foreach ($s in $svcs) {
        if ($s.StartMode -eq "Auto") { $stm = "automatic" } else { $stm = $s.StartMode.ToLower() }
        if ($s.State -eq "Running")  { $state = "started" } else { $state = $s.State.ToLower() }
        $s.Name.Replace(" ","_").PadRight(39) + " " + $stm.PadRight(12) + " " + $state.PadRight(14) + " " + $s.DisplayName
    }
    WriteLog "XymonSvcs finished."
}

function XymonProcs
{
    WriteLog "XymonProcs start"
	"[procs]"
	"{0,8} {1,-35} {2,-17} {3,-17} {4,-17} {5,8} {6,-7} {7,5} {8}" -f "PID", "User", "WorkingSet/Peak", "VirtualMem/Peak", "PagedMem/Peak", "NPS", "Handles", "%CPU", "Name"

    # one call to Get-WmiObject rather than as many calls as we have processes
    $wmiProcs = Get-WmiObject -Class Win32_Process

	foreach ($p in $procs) {
		if ($svcprocs[($p.Id)] -ne $null) {
			$procname = "Service:" + $svcprocs[($p.Id)]
		}
		else {
			$procname = $p.Name
		}

        #$thiswmip = Get-WmiObject -Query "select * from Win32_Process where ProcessId = $($p.Id)"
        $thiswmip = $wmiProcs | where { $_.ProcessId -eq $p.Id }
        $cmdline = "{0} {1}" -f $procname, $thiswmip.CommandLine
       
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
#		"{0,8} {1,-35} {2} {3} {4} {5} {6,7:F0} {7} {8}" -f $p.Id, $owner, $pws, $pvmem, $ppgmem, $pnpgmem, $p.HandleCount, $pcpu, $procname
		"{0,8} {1,-35} {2} {3} {4} {5} {6,7:F0} {7} {8}" -f $p.Id, $owner, $pws, $pvmem, $ppgmem, $pnpgmem, $p.HandleCount, $pcpu, $cmdline
	}
    WriteLog "XymonProcs finished."
}

function XymonWho
{
    WriteLog "XymonWho start"
	if( $HaveCmd.qwinsta) {
		"[who]"
		qwinsta.exe /counter
	}
    WriteLog "XymonWho finished."
}

function XymonUsers
{
    WriteLog "XymonUsers start"
	if( $HaveCmd.query) {
		"[users]"
		query user
	}
    WriteLog "XymonUsers finished."
}

function XymonIISSites
{
    WriteLog "XymonIISSites start"
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
        clear-variable objChild
	}
	clear-variable objSites
    WriteLog "XymonIISSites finished."
}

function XymonWMIOperatingSystem
{
	"[WMI:Win32_OperatingSystem]"
	WMIProp Win32_OperatingSystem
}

function XymonWMIQuickFixEngineering
{
    if ($script:XymonSettings.EnableWin32_QuickFixEngineering -eq 1)
    {
        "[WMI:Win32_QuickFixEngineering]"
        Get-WmiObject -Class Win32_QuickFixEngineering | where { $_.Description -ne "" } | Sort-Object HotFixID | Format-Wide -Property HotFixID -AutoSize
    }
    else
    {
        WriteLog "Skipping XymonWMIQuickFixEngineering, EnableWin32_QuickFixEngineering = 0 in config"
    }
}

function XymonWMIProduct
{
    if ($script:XymonSettings.EnableWin32_Product -eq 1)
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
    else
    {
        WriteLog "Skipping XymonWMIProduct, EnableWin32_Product = 0 in config"
    }
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

function XymonServiceCheck
{
    WriteLog "Executing XymonServiceCheck"
    if ($script:clientlocalcfg_entries -ne $null)
    {
        $servicecfgs = @($script:clientlocalcfg_entries.keys | where { $_ -match '^servicecheck' })
        foreach ($service in $servicecfgs)
        {
            # parameter should be 'servicecheck:<servicename>:<duration>'
            $checkparams = $service -split ':'
            # validation
            if ($checkparams.length -ne 3)
            {
                WriteLog "ERROR: config error (should be servicecheck:<servicename>:<duration>) - $service"
                continue
            }
            else
            {
                $duration = $checkparams[2] -as [int]
                if ($checkparams[1] -eq '' -or $duration -eq $null)
                {
                    WriteLog "ERROR: config error (should be servicecheck:<servicename>:<duration>) - $service"
                    continue
                }
            }

            WriteLog ("Checking service {0}" -f $checkparams[1])

            $winsrv = Get-Service -Name $checkparams[1]
            if ($winsrv.Status -eq 'Stopped')
            {
                writeLog ("Service {0} is stopped" -f $checkparams[1])
                if ($script:ServiceChecks.ContainsKey($checkparams[1]))
                {
                    $restarttime = $script:ServiceChecks[$checkparams[1]].AddSeconds($duration)
                    writeLog "Seen this service before; restart time is $restarttime"
                    if ($restarttime -lt (get-date))
                    {
                        writeLog ("Starting service {0}" -f $checkparams[1])
                        $winsrv.Start()
                    }
                }
                else
                {
                    writeLog "Not seen this service before, setting restart time -1 hour"
                    $script:ServiceChecks[$checkparams[1]] = (get-date).AddHours(-1)
                }
            }
            elseif ('StartPending', 'Running' -contains $winsrv.Status)
            {
                writeLog "Service is running, updating last seen time"
                $script:ServiceChecks[$checkparams[1]] = get-date
            }
        }
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

function XymonClientConfig($cfglines)
{
	if ($cfglines -eq $null -or $cfglines -eq "") { return }

	# Convert to Windows-style linebreaks
	$script:clientlocalcfg = $cfglines.Split("`n")

    # overwrite local cached config with this version if 
    # remote config is enabled
    if ($script:XymonSettings.clientremotecfgexec -ne 0)
    {
        WriteLog "Using new remote config, saving locally"
        $clientlocalcfg >$script:XymonSettings.clientconfigfile
    }
    else
    {
        WriteLog "Using local config only (if one exists), clientremotecfgexec = 0"
    }

	# Parse the config - always uses the local file (which may contain
    # config from remote)
	if (test-path -PathType Leaf $script:XymonSettings.clientconfigfile) 
    {
         $script:clientlocalcfg_entries = @{}
         $lines = get-content $script:XymonSettings.clientconfigfile
         $currentsection = ''
         foreach ($l in $lines)
         {
             # change this to recognise new config items
             if ($l -match '^eventlog:' -or $l -match '^servicecheck:' `
                 -or $l -match '^dir:' -or $l -match '^file:' `
                 -or $l -match '^dirsize:' -or $l -match '^dirtime:' `
                 -or $l -match '^log' -or $l -match '^clientversion:' `
                 -or $l -match '^eventlogswanted' `
                 -or $l -match '^servergifs:' `
                 )
             {
                 WriteLog "Found a command: $l"
                 $currentsection = $l
                 $script:clientlocalcfg_entries[$currentsection] = @()
             }
             elseif ($l -ne '')
             {
                 $script:clientlocalcfg_entries[$currentsection] += $l
             }
         }
    }
    WriteLog "Cached config now contains: "
    WriteLog ($script:clientlocalcfg_entries.keys -join ', ')

    # special handling for servergifs
    $gifpath = @($script:clientlocalcfg_entries.keys | where { $_ -match '^servergifs:(.+)$' })
    if ($gifpath.length -eq 1)
    {
        $script:XymonSettings.servergiflocation = $matches[1]
    }
}

function XymonReportConfig
{
	"[XymonConfig]"
	"XymonSettings"
	$script:XymonSettings
	""
	"HaveCmd"
	$HaveCmd
	foreach($v in @("XymonClientVersion", "clientname" )) {
		""; "$v"
		(Get-Variable $v).Value
	}
	"[XymonPSClientInfo]"
    $script:thisXymonProcess	
    #get-process -id $PID
	#"[XymonPSClientThreadStats]"
	#(get-process -id $PID).Threads
}

function XymonClientSections {
    XymonClientVersion
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

    if ($script:XymonSettings.EnableWMISections -eq 1)
    {
        XymonWMIOperatingSystem
        XymonWMIComputerSystem
        XymonWMIBIOS
        XymonWMIProcessor
        XymonWMIMemory
        XymonWMILogicalDisk
    }

    XymonServiceCheck
    XymonDirSize
    XymonDirTime

	$XymonIISSitesCache
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
	if((Get-Item -ea:SilentlyContinue $XymonRegKey) -eq $null) {
		$cfgitm = New-Item $XymonRegKey
	}
	Set-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters Application "$PSHOME\powershell.exe"
	Set-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters "Application Parameters" "-ExecutionPolicy RemoteSigned -NoLogo -NonInteractive -NoProfile -WindowStyle Hidden -File `"$scriptname`""
	Set-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\Parameters "Application Default" $xymondir
}

function XymonClientUnInstall()
{
    if ((Get-Service -ea:SilentlyContinue $xymonsvcname) -ne $null)
    {
        Stop-Service $xymonsvcname
        $service = Get-WmiObject -Class Win32_Service -Filter "Name='$xymonsvcname'"
        $service.delete()

        Remove-Item -Path HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\* -Recurse -ErrorAction SilentlyContinue
    }
}

function ExecuteSelfUpdate([string]$newversion)
{
    $oldversion = $MyInvocation.ScriptName

    WriteLog "Upgrading $oldversion to $newversion"

    # sleep to allow original script to exit
    # stop existing service
    # copy newversion to correct name
    # remove newversion file
    # re-start service

    $command = "sleep -seconds 5; if ((get-service '$xymonsvcname').Status -eq 'Running') { stop-service '$xymonsvcname' }; " +
        "copy-item '$newversion' '$oldversion' -force; remove-item '$newversion'; start-service '$xymonsvcname'"

    $StartInfo = new-object System.Diagnostics.ProcessStartInfo
    $StartInfo.Filename = join-path $pshome 'powershell.exe'

    # for debugging:
    # set .UseShellExecute to $true below
    # add -noexit to leave the upgrade window open
    # remove it to close the window at the end
    $StartInfo.Arguments = "-noprofile -executionpolicy RemoteSigned -Command `"$command`""
    $StartInfo.WorkingDirectory = $xymondir
    $StartInfo.LoadUserProfile = $true
    $StartInfo.UseShellExecute = $false
    $ret = [System.Diagnostics.Process]::Start($StartInfo)
    exit
}

function XymonCheckUpdate
{
    WriteLog "Executing XymonCheckUpdate"
    $updates = @($script:clientlocalcfg_entries.keys | where { $_ -match '^clientversion:(\d+\.\d+):(.+)$' })
    if ($updates.length -gt 1)
    {
        WriteLog "ERROR: more than one clientversion directive in config!"
    }
    elseif ($updates.length -eq 1)
    {
        # $matches[1] = the new version number
        # $matches[2] = the place to look for new version file
        if ($Version -lt $matches[1])
        {
            WriteLog "Running version $Version; config version $($matches[1]); attempting upgrade"

            $newversion = join-path $matches[2] "xymonclient_$($matches[1]).ps1"

            if (!(Test-Path $newversion))
            {
                WriteLog "New version $newversion cannot be found - aborting upgrade"
                return
            }

            WriteLog "Copying $newversion to $xymondir"
            Copy-Item  $newversion $xymondir -Force

            $newversion = Join-Path $xymondir (Split-Path $newversion -Leaf)

            WriteLog "Launching update"
            ExecuteSelfUpdate $newversion
        }
        else
        {
            WriteLog "Update: Running version $Version; config version $($matches[1]); doing nothing"
        }
    }
    else
    {
        # no clientversion directive
        WriteLog "Update: No clientversion directive in config, nothing to do"
    }
}

function WriteLog([string]$message)
{
    $datestamp = get-date -uformat '%Y-%m-%d %H:%M:%S'
    add-content -Path $script:XymonSettings.clientlogfile -Value "$datestamp  $message"
    Write-Host "$datestamp  $message"
}

##### Main code #####
$script:thisXymonProcess = get-process -id $PID
$script:thisXymonProcess.PriorityClass = "High"
XymonConfig
$ret = 0
# check for install/set/unset/config/start/stop for service management
if($args -eq "Install") {
	XymonClientInstall $MyInvocation.MyCommand.Definition
	$ret=1
}
if ($args -eq "uninstall")
{
    XymonClientUnInstall
    $ret=1
}
if($args[0] -eq "config") {
	"XymonPSClient config:`n"
    $XymonCfgLocation
	"Settable Params and values:"
	foreach($param in $script:XymonSettings | gm -memberType NoteProperty,Property) {
		if($param.Name -notlike "PS*") {
			$val = $script:XymonSettings.($param.Name)
			if($val -is [Array]) {
				$out = [string]::join(" ",$val)
			} else {
				$out = $val.ToString()
			}
			"    {0}={1}" -f $param.Name,$out
		}
	}
	return
}
if($args -eq "Start") {
	if((get-service $xymonsvcname).Status -ne "Running") { start-service $xymonsvcname }
	return
}
if($args -eq "Stop") {
	if((get-service $xymonsvcname).Status -eq "Running") { stop-service $xymonsvcname }
	return
}
if($ret) {return}
if($args -ne $null) {
	"Usage: "+ $MyInvocation.MyCommand.Definition +" install | uninstall | start | stop | config "
	return
}

# assume no other args, so run as normal

# elevate our priority to configured setting
$script:thisXymonProcess.PriorityClass = $script:XymonSettings.ClientProcessPriority

# ZB: read any cached client config
if (Test-Path -PathType Leaf $script:XymonSettings.clientconfigfile)
{
    $cfglines = (get-content $script:XymonSettings.clientconfigfile) -join "`n"
    XymonClientConfig $cfglines
}

$running = $true
$collectionnumber = (0 -as [long])
$loopcount = ($script:XymonSettings.slowscanrate - 1)

#Write-Host "Running as normal"
#Write-Host "clientname is " $clientname

while ($running -eq $true) {
    Set-Content -Path $script:XymonSettings.clientlogfile `
        -Value "$clientname - $XymonClientVersion"

    $collectionnumber++
    WriteLog "This is collection number $collectionnumber"

	$starttime = Get-Date
	
	$loopcount++ 
	if ($loopcount -eq $script:XymonSettings.slowscanrate) { 
		$loopcount = 0
        
        WriteLog "Doing slow scan tasks"

        XymonCheckUpdate

		WriteLog "Executing XymonWMIQuickFixEngineering"
        $XymonWMIQuickFixEngineeringCache = XymonWMIQuickFixEngineering
        WriteLog "Executing XymonWMIProduct"
		$XymonWMIProductCache = XymonWMIProduct
        WriteLog "Executing XymonIISSites"
		$XymonIISSitesCache = XymonIISSites

        WriteLog "Slow scan tasks completed."
	}

	XymonCollectInfo
    
    WriteLog "Performing main and optional tests and building output..."
	$clout = "client " + $clientname + ".bbwin win32 XymonPS $Version" | Out-String
	$clsecs = XymonClientSections | Out-String
	$localdatetime = Get-Date
	$clout += XymonDate | Out-String
	$clout += XymonClock | Out-String
	$clout +=  $clsecs
	
	#XymonReportConfig >> $script:XymonSettings.clientlogfile
    WriteLog "Main and optional tests finished."
	
    WriteLog "Sending to server"
    Set-Content -path c:\xymon-lastcollect.txt -value $clout
        
    $newconfig = XymonSend $clout $script:XymonSettings.servers
	XymonClientConfig $newconfig
	[GC]::Collect() # run every time to avoid memory bloat
    
	$delay = ($script:XymonSettings.loopinterval - (Get-Date).Subtract($starttime).TotalSeconds)
    WriteLog "Delaying until next run: $delay seconds"
	if ($delay -gt 0) { sleep $delay }
}
