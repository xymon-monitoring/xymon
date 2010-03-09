//this file is part of BBWin
//Copyright (C)2006-2008 Etienne GRIGNON  ( etienne.grignon@gmail.com )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Id: ProcInOut.cpp 70 2008-01-31 14:05:39Z sharpyy $

#include <windows.h>
#include <string>
#include "ProcInOut.h"

namespace utils {

bool		ProcInOut::Exec(const std::string & cmd, std::string & out, DWORD timeout) {
	bool	fSuccess; 

	ZeroMemory(&saAttr, sizeof(saAttr));
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 
	hSaveStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
	if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) 
		return false; 
	if (! SetStdHandle(STD_OUTPUT_HANDLE, hChildStdoutWr)) 
		return false; 
	fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
		GetCurrentProcess(), &hChildStdoutRdDup , 0,
		(FALSE ? false : true),
		DUPLICATE_SAME_ACCESS);
	if( !fSuccess )
		return false;
	CloseHandle(hChildStdoutRd);
	if (! CreateChildProcess(cmd)) {
		CloseHandle(hChildStdoutWr);
		CloseHandle(hChildStdoutRdDup);
		CloseHandle(piProcInfo.hProcess);
		CloseHandle(piProcInfo.hThread);
		return false;
	}
	if (! SetStdHandle(STD_OUTPUT_HANDLE, hSaveStdout)) 
		return false;
	if (!CloseHandle(hChildStdoutWr)) 
		return false;
	HANDLE			lpHandles[] = { piProcInfo.hProcess, hChildStdoutRdDup, NULL};
	DWORD			dwWait;
	DWORD			timerStart = GetTickCount();
	DWORD			timerEnd;
	for (;;) {
		dwWait = WaitForMultipleObjects(2, lpHandles, false, timeout);
		// process finished
		if (dwWait == WAIT_OBJECT_0 || dwWait == WAIT_ABANDONED_0 || dwWait == (WAIT_ABANDONED_0 + 1)) {
			break;
		}
		if (dwWait == (WAIT_OBJECT_0 + 1)) {
			timerEnd = GetTickCount();
			// timeout
			if (timerEnd < timerStart && ((MAXDWORD - timerStart) + timerEnd) >= timeout) // counter has been reinitialized
				break ;
			if (timerStart < timerEnd && (timerEnd - timerStart) >= timeout)
				break ;
			ReadFromPipe(out); 
		}
		if (dwWait == WAIT_TIMEOUT) {
			break;			
		}
	}
	CloseHandle(hChildStdoutRdDup);
	CloseHandle(piProcInfo.hProcess);
	CloseHandle(piProcInfo.hThread);
	return true; 
}

bool		ProcInOut::CreateChildProcess(const std::string & cmd) {
   STARTUPINFO siStartInfo; 
 
   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   SecureZeroMemory( &piProcInfo, sizeof(piProcInfo));
   siStartInfo.cb = sizeof(STARTUPINFO); 
   return CreateProcess(NULL, 
      (LPSTR)cmd.c_str(),       // command line 
      NULL,          // process security attributes 
      NULL,          // primary thread security attributes 
      TRUE,          // handles are inherited 
      0,             // creation flags 
      NULL,          // use parent's environment 
      NULL,          // use parent's current directory 
      &siStartInfo,  // STARTUPINFO pointer 
      &piProcInfo);  // receives PROCESS_INFORMATION 
}

void			ProcInOut::ReadFromPipe(std::string & out) {
	DWORD		dwRead;
	CHAR		chBuf[BUFSIZE]; 
	HANDLE		hStdout = GetStdHandle(STD_OUTPUT_HANDLE); 

	std::string	buf;
	if( !ReadFile( hChildStdoutRdDup, chBuf, BUFSIZE, &dwRead, 
		NULL) || dwRead == 0) 
		return ; 
	chBuf[dwRead] = '\0';
	buf = chBuf;
	out += buf;
} 

} // namespace utils

