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
// $Id: IBBWinAgent.h 96 2008-05-21 17:30:49Z sharpyy $


#ifndef __IBBWINAGENT_H__
#define __IBBWINAGENT_H__

#include "IBBWinAgentManager.h"

//
// Log levels
//
#define			LOGLEVEL_ERROR		1
#define			LOGLEVEL_WARN		2
#define 		LOGLEVEL_INFO		3
#define			LOGLEVEL_DEBUG		4

//
// agent version define : must be used in BBWinAgentInfo_s structure
//
#define			BBWIN_AGENT_VERSION			0x01

//
// Agent Flags
// must be set to 0 if no flag is set
//
#define			BBWIN_AGENT_THREAD_SAFE					0x01
#define			BBWIN_AGENT_CENTRALIZED_COMPATIBLE			0x02


// 
// agent information structure
// used for future compatibility
//
typedef struct			BBWinAgentInfo_s
{
	DWORD				bbwinVersion;			/* BBWin version to check the agent compatibilities */
	const char			*agentName;				/* Agent Name */
	const char 			*agentDescription;		/* Agent Description */
	DWORD				agentFlags;				/* Agent Flags for its working mode */
}						BBWinAgentInfo_t;

//
// Agent module class
//
class IBBWinAgent 
{
	public :
		virtual ~IBBWinAgent() {};
		// init function called only once after loaded the agent, return true on success, Agent will be unloaded if false returned
		virtual bool Init() = 0;
		// run method executed repetitively depending the timer setting
		virtual void Run() = 0;
};
	
#ifdef BBWIN_AGENT_EXPORTS
	#define BBWIN_AGENTDECL __declspec(dllexport)
#else
	#define BBWIN_AGENTDECL __declspec(dllimport)
#endif

typedef  IBBWinAgent  *(*CREATEBBWINAGENT)(IBBWinAgentManager & mgr);
typedef  void			(*DESTROYBBWINAGENT)(IBBWinAgent * agent);
typedef  const BBWinAgentInfo_t * (*GETBBWINAGENTINFO)();

extern "C" {
	BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr);
}

extern "C" {
	BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent);
}

extern "C" {
	BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo();
}

#endif // !__IBBWINAGENT_H__

