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
// $Id: stats.h 60 2008-01-14 12:20:01Z sharpyy $

#ifndef		__STATS_H__
#define		__STATS_H__

#include "IBBWinAgent.h"

class AgentStats : public IBBWinAgent
{
	private :
		IBBWinAgentManager 		& m_mgr;
		std::string				m_testName;
		
	private :
		void		NetstatLocal(const std::string & path, std::stringstream & reportData);
		void		NetstatCentralized(const std::string & path);
		void		NetstatCentralizedStep(const std::string & cmd, const std::string & path, 
			const std::string dataName);
		void		IfStat(std::stringstream & reportData);

	public :
		AgentStats(IBBWinAgentManager & mgr);
		bool Init();
		void Run();
};


#endif 	// !__STATS_H__

