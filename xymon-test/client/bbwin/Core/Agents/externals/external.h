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

#ifndef		__EXTERNAL_H__
#define		__EXTERNAL_H__

#include <windows.h>

#include <string>

#include "IBBWinAgentManager.h"

#include "ou_thread.h"
using namespace openutils;

#define ERROR_STR_LEN			1024

class	External : public Thread {
	private :
		DWORD				m_timer;
		const std::string	m_extCommand;	
		HANDLE				*m_hEvents;
		DWORD				m_hCount;
		IBBWinAgentManager 	& m_mgr;
		STARTUPINFO 			m_startupInfo;
		PROCESS_INFORMATION 	m_procInfo;
	
	private :
		bool		LaunchExternal();
	
	public :
		External(IBBWinAgentManager & mgr, const string extCommand, DWORD timer);
		void 	run();
};


#endif  // !__EXTERNAL_H__
