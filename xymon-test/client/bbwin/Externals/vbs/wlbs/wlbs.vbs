'***********************************************************************************
'* this file is part of BBWin
'*Copyright (C)2006 Etienne GRIGNON  ( etienne.grignon@gmail.com )
'*
'*This program is free software; you can redistribute it and/or
'*modify it under the terms of the GNU General Public License
'*as published by the Free Software Foundation; either
'*version 2 of the License, or (at your option) any later version.
'*
'*This program is distributed in the hope that it will be useful,
'*but WITHOUT ANY WARRANTY; without even the implied warranty of
'*MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
'*GNU General Public License for more details.
'*
'*You should have received a copy of the GNU General Public License
'*along with this program; if not, write to the Free Software
'*Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
'*
'*
'* File:		WLBS.VBS
'* Created:		February 15, 2006
'* Last Modified:	August 30, 2006
'* Version:		1.1
'*
'* Main Function:  	check wlbs (nlb)  node status
'* 
'* System Requirement : Windows 2000 Advanced Server with NLB installed and enabled
'*                      Windows 2003 Server with NLB installed and enabled
'*
'* Dependencies : use the nlb wmi provider
'* 
'*
'* History
'* 2006/02/17      1.0          First try
'* 2006/08/30      1.1          resolve a bug for the configuration loading, it wasn't the right path
'***********************************************************************************

' use explicit declaration
OPTION EXPLICIT

' use resume next
On Error Resume Next

'********************************************************************
'* Globals
'********************************************************************
Dim 	shello ' Shell object to get registry value
Dim 	fso ' file system object to open the file
Dim 	extPath ' Path to save the result 
Dim		regTmp, regConf ' registry keys for BBWin
Dim		debugFlag ' debug flag
Dim		alertColor ' BB color used 
Dim		columnName ' BB name column 

alertColor = "yellow"
columnName = "wlbs"

regTmp = "HKLM\SOFTWARE\BBWin\tmpPath"
regConf = "HKLM\SOFTWARE\BBWin\etcPath"

'********************************************************************
'* Debug Flag
'********************************************************************
debugFlag = false

'********************************************************************
'* Consts
'********************************************************************
Const WLBS_STOPPED = 1005
Const WLBS_CONVERGING = 1006
Const WLBS_CONVERGED = 1007
Const WLBS_DEFAULT = 1008
Const WLBS_DRAINING = 1009
Const WLBS_SUSPENDED = 1013

'* RegExp Pattern
Dim C_PatternConfig
C_PatternConfig = "^\s*DEFAULTNODESTATUS\s+(\w+)\s*$"

Set 	shello = WScript.CreateObject("WScript.Shell")
Set 	fso = CreateObject("Scripting.FileSystemObject")

'********************************************************************
'*
'* Sub Debug()
'* Purpose: Debug function
'* Input:   
'* Output:  
'*
'********************************************************************
Sub			Debug(msg)
	If debugFlag = true Then
		WScript.Echo msg
	End If
End Sub


'********************************************************************
'*
'* Sub ReadConfig()
'* Purpose: read config file
'* Input:   
'* Output:  
'*
'********************************************************************
Sub				ReadConfig(byref confStatus)
	Dim 		regEx ' regexp object 
	Dim 		match ' match results
	Dim			fconf ' file object
	Dim			line ' line
	Dim			etcPath

	On Error Resume Next
	etcPath = shello.RegRead(regEtc)
	Err.Number = 0
	Set fconf = fso.OpenTextFile(etcPath & "\wlbs.cfg", 1)
	If Err.Number <> 0 Then
		Debug("Can't OpenTextFile " & C_ConfigPath)
		Exit Sub
	End If
	Set regEx = New RegExp
	regEx.Pattern = C_PatternConfig
	regEx.IgnoreCase = True
	regEx.Global = True
	If Err.Number = 0 Then
		Do While fconf.AtEndOfStream <> True
			line = fconf.ReadLine
			Dim comPos
			comPos = InStr(line, "#")
			If comPos > 0 Then
				line = Left(line, comPos - 1)
			End If
			If Len(line) > 0 Then
				If regEx.test(line) = True Then
					Set match = regEx.Execute(line)
					confStatus = match(0).SubMatches(0)
				End If
			End If
		Loop
	End If
	Set regEx = nothing
End Sub

'********************************************************************
'*
'* Sub GetWlbsStatusText()
'* Purpose: GetWlbsStatusText
'* Input:   statusCode 
'*		
'* Output:  
'*
'********************************************************************
Function			GetWlbsStatusText(statusCode)
	Select Case statusCode
		Case WLBS_STOPPED GetWlbsStatusText = "stopped"
		Case WLBS_CONVERGING GetWlbsStatusText = "converging"
		Case WLBS_CONVERGED GetWlbsStatusText = "converged"
		Case WLBS_DEFAULT GetWlbsStatusText = "started"
		Case WLBS_DRAINING GetWlbsStatusText = "draining"
		Case WLBS_SUSPENDED GetWlbsStatusText = "suspended"
		Case Else GetWlbsStatusText = "unknown " & statusCode
	End Select
End Function 
	
'********************************************************************
'*
'* Sub GenerateReport()
'* Purpose: Entry point to generate BB Report
'* Input:   defaultStatus
'* Output:  
'*
'********************************************************************
Sub 	GenerateReport(defaultStatus)
	Dim 	objWMIService ' wmi provider
	Dim 	colItems ' wmi collection
	Dim 	objItem ' wmi object item
	Dim		DedicatedIPAddress ' node ip adress
	Dim 	StatusCode ' statuscode node
	Dim		HostPriority ' node priority
	Dim		Name ' node name
	Dim		tmpPath ' tmpPath
	Dim		f ' result file object
	Dim		strAlarmState ' alarm type 
	Dim		outPut ' out put buffer
	
	On Error Resume Next
	Err.Number = 0
	Set objWMIService = GetObject("winmgmts:" _
	    & "{impersonationLevel=impersonate}!\\.\root\MicrosoftNLB")
	If Err.Number <> 0 Then ' can't get the provider
		Debug("Can't get MicrosoftNLB wmi provider")
		WScript.Quit(0)
	End If
	Err.Number = 0
	Set colItems = objWMIService.ExecQuery("Select * from MicrosoftNLB_Node ")
	If Err.Number <> 0 Then ' can't query
		WScript.Quit(0)
	End If
	For Each objItem in colItems
		DedicatedIPAddress = objItem.DedicatedIPAddress
		HostPriority = objItem.HostPriority
	    Name = objItem.Name
		StatusCode = objItem.StatusCode
	Exit For
	Next
	If StatusCode <> defaultStatus Then
		strAlarmState = alertColor
	Else
		strAlarmState = "green"
	End If
	Err.Number = 0
	tmpPath = shello.RegRead(regTmp)
	If Err.Number <> 0 Then
		Debug("Can't get " & regTmp)
		WScript.Quit(0)
	End If
	outPut = strAlarmState & " " & Date & " " & Time & vbcrlf & vbcrlf & columnName & " status" & vbcrlf
	outPut = outPut & vbcrlf & "IP : " & DedicatedIPAddress & vbcrlf
	outPut = outPut & vbcrlf & "Name : " & Name & vbcrlf
	outPut = outPut & vbcrlf & "Prority : " & HostPriority & vbcrlf
	outPut = outPut & vbcrlf & "&green Default Status : " & GetWlbsStatusText(defaultStatus) & vbcrlf
	outPut = outPut & vbcrlf & "&" &  strAlarmState & " Current Status : " & GetWlbsStatusText(StatusCode) & vbcrlf
	outPut = outPut & vbcrlf & vbcrlf
	Set f = fso.OpenTextFile(extPath & "\" & columnName, 8 , True)
	f.Write outPut
	f.Close
	Debug(StatusCode)
	Debug(HostPriority)
	Debug(DedicatedIPAddress)
	Debug(Name)
End Sub

'********************************************************************
'*
'* Sub nlb()
'* Purpose: get nlb node status
'* Input:   
'* Output:  
'*
'********************************************************************
Sub 		wlbs()
	Dim		DefaultStatusCode ' default status code
	Dim		conf ' default conf string
	
	Debug("wlbs started")
	conf = ""
	DefaultStatusCode = WLBS_DEFAULT
	call ReadConfig(conf)
	conf = LCase(conf)
	Select Case conf
		Case "started" DefaultStatusCode = WLBS_DEFAULT
		Case "stopped" DefaultStatusCode = WLBS_STOPPED
		Case "suspended" DefaultStatusCode = WLBS_SUSPENDED
		Case "converged" DefaultStatusCode = WLBS_CONVERGED
	End Select
	Err.Number = 0
	extPath = shello.RegRead(regTmp)
	If Err.Number <> 0 Then ' can't get registry
		Debug("Can't RegRead " & C_ConfigPath)
		WScript.Quit(0)
	End If
	GenerateReport(DefaultStatusCode)
	Debug("wlbs end")
End Sub


'********************************************************************
'*
'* Sub Main()
'* Purpose: Entry point for the main function
'* Input:   args      The Arguments object
'* Output:  
'*
'********************************************************************
Sub			Main(byref args)
	call wlbs()
End Sub


'********************************************************************
'* Main call
'********************************************************************

Main(WScript.Arguments)

Set shello = nothing
Set fso = nothing

'********************************************************************
'*                                                                  *
'*                           End of File                            *
'*                                                                  *
'********************************************************************
