//this file is part of BBWin
//Copyright (C)2006 Etienne GRIGNON  ( etienne.grignon@gmail.com )
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

#include <windows.h>

#include <string>
#include <sstream>
#include <fstream>
#include <iostream>

using namespace std;

#include "IBBWinAgent.h"
#include "External.h"



External::External(IBBWinAgentManager & mgr, const string extCommand, DWORD timer) :
			m_mgr (mgr),
			m_extCommand (extCommand),
			m_timer (timer)
{
	Thread::setName(extCommand.c_str());
}

bool	External::LaunchExternal() {
	stringstream 		dbgMess;

	ZeroMemory(&m_startupInfo, sizeof(m_startupInfo) );
    m_startupInfo.cb = sizeof(m_startupInfo);
	ZeroMemory(&m_procInfo, sizeof(m_procInfo));
	dbgMess << "starting external '" << m_extCommand << "'";
	m_mgr.Log(LOGLEVEL_DEBUG, dbgMess.str().c_str());
	if (!CreateProcess(0, const_cast <LPTSTR> (m_extCommand.c_str()), 0, 0, false, 0, 0, NULL, &m_startupInfo, &m_procInfo))
	{
		TCHAR		buf[ERROR_STR_LEN + 1];
		string	err;
		string 	mess;
		
		m_mgr.GetLastErrorString(buf, ERROR_STR_LEN);
		err = buf;
		mess = "Failed to launch " + m_extCommand + ": " + err;
		m_mgr.ReportEventWarn(mess.c_str());
		return false;
	}
	return true;
}

void 	External::run() {
	m_hCount = m_mgr.GetHandlesCount();
	
	try {
		m_hEvents = new HANDLE[m_hCount + 2];
	} catch (std::bad_alloc ex) {
		return ;
	}
	if (m_hEvents == NULL)
		return ;
	m_mgr.GetHandles(m_hEvents);
	for (;;) {
		DWORD 		dwWait;
		bool		over = false;

		if (LaunchExternal() == false) {
			break ;
		}
		m_hEvents[m_hCount] = m_procInfo.hProcess;
		dwWait = WaitForMultipleObjects(m_hCount + 1, m_hEvents , FALSE, INFINITE );
		if ( dwWait >= WAIT_OBJECT_0 && dwWait <= (WAIT_OBJECT_0 + m_hCount - 1)) {  
			if (m_procInfo.hProcess)
				CloseHandle(m_procInfo.hProcess);
			if (m_procInfo.hThread)
				CloseHandle(m_procInfo.hThread);
			break ;
		} else if (dwWait >= WAIT_ABANDONED_0 && dwWait <= (WAIT_ABANDONED_0 + m_hCount - 1)) {
			if (m_procInfo.hProcess)
				CloseHandle(m_procInfo.hProcess);
			if (m_procInfo.hThread)
				CloseHandle(m_procInfo.hThread);
			break ;
		} 
		if (m_procInfo.hProcess)
			CloseHandle(m_procInfo.hProcess);
		if (m_procInfo.hThread)
			CloseHandle(m_procInfo.hThread);		
		dwWait = WaitForMultipleObjects(m_hCount, m_hEvents , FALSE, m_timer * 1000);
		if ( dwWait >= WAIT_OBJECT_0 && dwWait <= (WAIT_OBJECT_0 + m_hCount - 1) ) {    
			break ;
		} else if (dwWait >= WAIT_ABANDONED_0 && dwWait <= (WAIT_ABANDONED_0 + m_hCount - 1) ) {
			break ;
		}
	}
	delete [] m_hEvents;
}
