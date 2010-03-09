//this file is part of BBWin
//Copyright (C)2007 Etienne GRIGNON  ( etienne.grignon@gmail.com )
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
// $Id: who.h 83 2008-02-11 17:35:53Z sharpyy $

#ifndef		__WHO_H__
#define		__WHO_H__

#include <string>
#include "IBBWinAgent.h"

#define		TEMP_PATH_LEN		1024

class AgentWho : public IBBWinAgent
{
	private :
		IBBWinAgentManager 		& m_mgr;
		std::string				m_testName;
		void					PrintWin32Error(LPSTR ErrorMessage, DWORD ErrorCode);
	public :
		AgentWho(IBBWinAgentManager & mgr);
		bool Init();
		void Run();
};


#endif 	// !__WHO_H__

