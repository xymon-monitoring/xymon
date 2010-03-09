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

#ifndef		__EXTERNALS_H__
#define		__EXTERNALS_H__

#include <vector>

#include "IBBWinAgent.h"
#include "External.h"

#define EXTERNAL_DEFAULT_LOG_TIMER 		30

class AgentExternals : public IBBWinAgent
{
	private :
		IBBWinAgentManager 		& m_mgr;
		HANDLE					*m_hEvents;
		DWORD					m_hCount;
		DWORD					m_timer;
		DWORD					m_logsTimer;
		std::vector<External *>		m_externals;
		
	private :
		void 		SendExternalsReports();
		void		SendExternalReport(const std::string & reportName, const std::string & reportPath);
		
	public :
		AgentExternals(IBBWinAgentManager & mgr);
		bool Init();
		void Run();
		
		void 	* operator new (size_t sz);
		void	operator delete (void * ptr);
		
};


#endif 	// __EXTERNALS_H__

