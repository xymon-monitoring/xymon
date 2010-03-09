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



#ifndef  __BBWINCENTRALHANDLER__H__
#define  __BBWINCENTRALHANDLER__H__

#include <list>

#include "IBBWinException.h"
#include "BBWinAgentManager.h"
#include "IBBWinAgent.h"
#include "Logging.h"

#include "BBWinHandlerData.h"

void		uname(std::string & version);

// 
// inherit from the nice thread class written by Vijay Mathew Pandyalakal
// this class handle each agent and execute agent code
// 
class BBWinCentralHandler : public openutils::Thread {
	private:
		std::list<BBWinHandler *>		m_agents;
		bbwinhandler_data_t				m_data;
		DWORD							m_timer;
		HANDLE							*m_hEvents;
		DWORD							m_hCount;
		Logging							*m_log;
		BBWinAgentManager				*m_mgr;
		bbdisplay_t						& m_bbdisplay;

	private :
		// RunAgents
		void	GetClock();
		// SendReport


	public:
		BBWinCentralHandler(bbwinhandler_data_t & data);
		~BBWinCentralHandler();
		DWORD	GetTimer() const { return m_timer; }
		void	SetTimer(DWORD timer) { m_timer = timer; }
		void	SetEventCount(DWORD count) { m_hCount = count; }
		void	SetEvents(HANDLE *events) { m_hEvents = events; }
		void	AddAgentHandler(BBWinHandler *handler);
		static	void bbwinClientData_callback(const std::string & dataName, const std::string & data);
		void run();
};


#endif // __BBWINCENTRALHANDLER__H__

