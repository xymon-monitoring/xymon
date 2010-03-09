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
'* File:		FSMON.VBS
'* Created:		March 15, 2006
'* Last Modified:	March 27, 2006
'* Version:		1.0
'*
'* Main Function:  	check file system directories or files
'* 
'* System Requirement : 
'*                     Windows 2000 
'*                     Windows 2003 
'*                     Windows XP
'*                     Windows NT 4
'*     
'* Dependencies : no
'* 
'*
'* History
'* 2006/03/15      1.0          First try
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
Dim		columnName ' BB name column 

columnName = "fsmon"

regTmp = "HKLM\SOFTWARE\BBWin\tmpPath"
regConf = "HKLM\SOFTWARE\BBWin\etcPath"

'********************************************************************
'* Debug Flag
'********************************************************************
debugFlag = false

'********************************************************************
'* Consts
'********************************************************************

'* RegExp Pattern
Dim C_PatternConfig
C_PatternConfig = "^\s*(\S+)\s*(.*)\s*"

'* BB Syntax
Const 		GREEN = 0, YELLOW = 1, RED = 2
Dim 	BBCOLORS 
BBCOLORS = Array("green", "yellow", "red")

'********************************************************************
'*
'*  Lines config
'*
'********************************************************************

Dim BBLines
Set BBLines = CreateObject("Scripting.Dictionary")


'********************************************************************
'*
'*  Dir Module Variables and Class
'*
'*
'********************************************************************
Dim C_DirPatternConfig ' directory configuration 
C_DirPatternConfig = "^\s*(\S+)\s*""(.*)"" (\S+) (\S+) ""(.*)""\s*"



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
'* Sub ExecDirRule()
'* Purpose: exec the directory directive rule to check validity
'* Input:   value to compare
'*              rule value
'* Output:  true is ok (false if rule not validated)
'*
'********************************************************************
Function 		ExecDirRule(value, rule) 
	Dim			ruleValue
	Dim			op
	Dim			res
	
	ExecDirRule = true
	ruleValue = Mid(rule, 2)
	op = Mid(rule, 1, 1)
	ruleValue = CInt(ruleValue)
	res = false
	Select Case op
		Case "<" 		res = CBool(value < ruleValue)
		Case ">" 		res = CBool(value > ruleValue)
	    Case Else 		res = CBool(value = ruleValue)
	End Select
	ExecDirRule = res
End Function


'********************************************************************
'*
'* Sub ReadDirConfig()
'* Purpose: read dir config file
'* Input:   		rule id (unique number used to insert in the hash table)
'* 			line to parse
'* Output:  
'*
'********************************************************************
Sub 			ReadExecDirConfig(id, line)
	Dim 		regEx ' regexp object 
	Dim 		match ' match results
	Set regEx = New RegExp
	Const 		C_name = 0, C_path = 1, C_warnrule = 2, C_panicrule = 3, C_desc = 4
	Dim 		dir

	regEx.Pattern = C_DirPatternConfig
	regEx.IgnoreCase = True
	regEx.Global = True
	If regEx.test(line) = True Then
		Dim 	color, res, str
		
		color = GREEN
		Set match = regEx.Execute(line)
		Err.Number = 0
		Set dir = fso.GetFolder(match(0).SubMatches(C_path))
		If Err.Number <> 0 Then
			Exit Sub
		End If
		res = ExecDirRule(dir.Files.Count, match(0).SubMatches(C_warnrule)) 
		If res = false Then
			color = YELLOW
		End If
		res = ExecDirRule(dir.Files.Count, match(0).SubMatches(C_panicrule)) 
		If res = false Then
			color = RED
		End If
		str = "&" & BBCOLORS(color) & " checking directory " & match(0).SubMatches(C_name) & " '" _
			& match(0).SubMatches(C_path) & "' - Rules are " & match(0).SubMatches(C_warnrule) & " and " & match(0).SubMatches(C_panicrule) _
			& " - Actually " &  dir.Files.Count & " file(s)"
		If color <> GREEN Then
			str = str & " - " & match(0).SubMatches(C_desc)
		End If
		call BBLines.Add("directory" & id, str)
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
Sub				ReadExecConfig()
	Dim 		regEx ' regexp object 
	Dim 		match ' match results
	Dim			fconf ' file object
	Dim			line ' line
	Dim			etcPath
	Dim			id
	
	On Error Resume Next
	etcPath = shello.RegRead(regConf)
	Err.Number = 0
	Set fconf = fso.OpenTextFile(etcPath & "\fsmon.cfg", 1)
	If Err.Number <> 0 Then
		Debug("Can't OpenTextFile " & etcPath & "\fsmon.cfg")
		Exit Sub
	End If
	On Error goto 0
	Set regEx = New RegExp
	regEx.Pattern = C_PatternConfig
	regEx.IgnoreCase = True
	regEx.Global = True
	id = 0
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
					'*
					'* Here add the other modules configuration loading procedure calls
					'*
					If match(0).SubMatches(0) = "DIR" Then
						call ReadExecDirConfig(id, match(0).SubMatches(1))
					End If
					id = id + 1
				End If
			End If
		Loop
	End If
	Set regEx = nothing
End Sub

'********************************************************************
'*
'* Sub GenerateReport()
'* Purpose: Entry point to generate BB Report
'* Input:   
'* Output:  
'*
'********************************************************************
Sub 	GenerateReport()
	Dim		line
	Dim		finalStatus
	Dim		freport
	Dim 	WshNetwork
	
	If BBLines.Count = 0 Then
		Exit Sub
	End If
	Set WshNetwork = WScript.CreateObject("WScript.Network")
	extPath = shello.RegRead(regTmp)
	Err.Number = 0
	Set freport = fso.OpenTextFile(extPath & "\" & columnName, 2 , True)
	If Err.Number <> 0 Then
		Exit Sub
	End If
	finalStatus = GREEN
	For Each line In BBLines 
		If InStr(Left(BBLines.Item(line), 7), BBCOLORS(YELLOW)) <> 0 Then
			finalStatus = YELLOW
		End If
		If InStr(Left(BBLines.Item(line), 7), BBCOLORS(RED)) <> 0 Then
			finalStatus = RED
		End If
	Next
	freport.WriteLine(BBCOLORS(finalStatus) & " " & Date & " " & Time & " [" & WshNetwork.ComputerName & "] ")
	freport.WriteLine("")
	For Each line In BBLines 
		freport.WriteLine(BBLines.Item(line))
	Next
	freport.Close
End Sub

'********************************************************************
'*
'* Sub fsmon()
'* Purpose: fsmon entry point
'* Input:   
'* Output:  
'*
'********************************************************************
Sub 		fsmon()
	Debug("fsmon started")
	call ReadExecConfig()
	Err.Number = 0
	If Err.Number <> 0 Then ' can't get registry
		Debug("Can't RegRead " & C_ConfigPath)
		WScript.Quit(0)
	End If
	GenerateReport()
	Debug("fsmon end")
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
	call fsmon()
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
