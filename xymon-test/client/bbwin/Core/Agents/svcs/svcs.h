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
// $Id: svcs.h 95 2008-05-21 17:19:38Z sharpyy $

#ifndef		__SVCS_H__
#define		__SVCS_H__

#include "IBBWinAgent.h"


#define 	MAX_SERVICE_LENGTH		1024

enum BBAlarmType { GREEN, YELLOW, RED };

class SvcRule {
	private :
		std::string		m_name;
		BBAlarmType		m_alarmColor;
		DWORD			m_state;
		bool			m_reset;
		std::string		m_comment;
		
	public :
		SvcRule();
		SvcRule(const SvcRule & rule);
		const bool	GetReset() const { return m_reset; };
		void 		SetReset(bool reset)  { m_reset = reset; };
		void		SetComment(const std::string & comment) { m_comment = comment; } ;
		const std::string & GetComment() const { return m_comment; } ;
		const std::string & GetName() const { return m_name; };
		void		SetName(const std::string & name) { m_name = name; };
		const BBAlarmType	GetAlarmColor() const { return m_alarmColor; };
		void 		SetAlarmColor(const BBAlarmType color) { m_alarmColor = color; };
		DWORD		GetSvcState() const { return m_state; };
		void		SetSvcState(const DWORD state) { m_state = state; };
};

class AgentSvcs : public IBBWinAgent
{
	private :
		IBBWinAgentManager 			& m_mgr;
		BBAlarmType					m_alarmColor;
		std::string					m_testName;
		BBAlarmType					m_pageColor;
		bool						m_alwaysGreen;
		bool						m_autoReset; // restart automatically automatic services that are stopped
		DWORD						m_delay; // time to wait before the svcs start if a computer restart has been done
		std::map<std::string, SvcRule>	m_rules;
		
	private :
		bool		InitCentralMode();
		void		CheckServices(std::stringstream & reportData, std::stringstream & autoRestartReportData);
		void		CheckSimpleService(SC_HANDLE & scm, LPCTSTR name, 
										std::stringstream & reportData, std::stringstream & autoRestartReportData);
		
	public :
		AgentSvcs(IBBWinAgentManager & mgr);
		~AgentSvcs();
		bool Init();
		void Run();
};


#endif 	// !__SVCS_H__
