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

#ifndef		__PROCS_H__
#define		__PROCS_H__

#include <string>
#include <list>
		
#include "IBBWinAgent.h"

#define	MAX_TABLE_PROC	1024

enum BBAlarmType { GREEN, YELLOW, RED };

//
// user rules
// 
struct ProcRule_s
{
	std::string 	name;
	std::string		rule;
	std::string		comment;
	BBAlarmType		color;
	DWORD			count;
	bool			(*apply_rule)(DWORD cur, DWORD rule);
};

//
// global rules checkers
// 
struct	Rule_s {
	const char 			*name;
	bool				(*apply_rule)(DWORD cur, DWORD rule);
};

//
// usefull typedef
// 
typedef struct ProcRule_s		ProcRule_t;
typedef struct Rule_s			Rule_t;


class AgentProcs : public IBBWinAgent
{
	private :
		IBBWinAgentManager 				& m_mgr;
		std::list<ProcRule_t *>			m_rules;
		std::string						m_testName;

	private :
		void			AddRule(const std::string & name, const std::string & rule, const std::string & color, const std::string & comment);
		BBAlarmType  	ExecProcRules(std::stringstream & reportData);
		
	public :
		AgentProcs(IBBWinAgentManager & mgr);
		~AgentProcs();
		bool Init();
		void Run();
};


#endif 	// !__PROCS_H__
