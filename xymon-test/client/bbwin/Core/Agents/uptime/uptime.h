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
//
// $Id: uptime.h 45 2007-12-28 10:17:58Z sharpyy $

#ifndef		__UPTIME_H__
#define		__UPTIME_H__

#include "IBBWinAgent.h"

#define	 UPTIME_DELAY		"30m"
#define	 UPTIME_MAX_DELAY	"365d"

class AgentUptime : public IBBWinAgent
{
	private :
		IBBWinAgentManager & m_mgr;
		DWORD					m_delay;
		DWORD					m_maxDelay;
		std::string				m_alarmColor;
		std::string				m_testName;
		
	public :
		AgentUptime(IBBWinAgentManager & mgr);
		bool Init();
		void Run();
};


#endif 	// !__UPTIME_H__

