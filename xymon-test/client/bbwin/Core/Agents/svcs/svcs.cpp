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
// $Id: svcs.cpp 95 2008-05-21 17:19:38Z sharpyy $

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <map>
#include "SystemCounters.h"
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"
#include "utils.h"
#include "svcs.h"

using namespace std;
using namespace boost::posix_time;
using namespace boost::gregorian;
using boost::format;

static const BBWinAgentInfo_t 		svcsAgentInfo =
{
	BBWIN_AGENT_VERSION,					// bbwinVersion;
	"svcs",									// agentName;
	"svcs agent : check Windows services",  // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE		// flags
};                

//
// common bb colors
//
static const char	*bbcolors[] = {"green", "yellow", "red", NULL};


static const struct _svcStatus {
	LPCSTR		name;
	DWORD		status;
} svcStatusTable [] = {
	{"continue_pending", SERVICE_CONTINUE_PENDING},
	{"pause_pending", SERVICE_PAUSE_PENDING},
	{"paused", SERVICE_PAUSED},
	{"started", SERVICE_RUNNING},
	{"start_pending", SERVICE_START_PENDING},
	{"stop_pending", SERVICE_STOP_PENDING},
	{"stopped", SERVICE_STOPPED},
	{NULL, 0}
};

LPCSTR		findSvcStatus(DWORD status) {
	for (DWORD inc = 0; svcStatusTable[inc].name != NULL; inc++) {
		if (svcStatusTable[inc].status == status)
			return svcStatusTable[inc].name;
	}
	return NULL;
}

//
//
// SvcRule Constructor
//
SvcRule::SvcRule() :
		m_name(""),
		m_alarmColor(YELLOW), 
		m_state(SERVICE_RUNNING),
		m_reset(false)
{
	
}

//
//
// SvcRule Copy Constructor
//
SvcRule::SvcRule(const SvcRule & rule) {
	m_alarmColor = rule.GetAlarmColor();
	m_state = rule.GetSvcState();
	m_name = rule.GetName();
	m_reset = rule.GetReset();
	m_comment = rule.GetComment();
}


bool		AgentSvcs::InitCentralMode() {
	string clientLocalCfgPath = m_mgr.GetSetting("tmppath") + (string)"\\clientlocal.cfg";

	m_mgr.Log(LOGLEVEL_DEBUG, "start %s", __FUNCTION__);
	m_mgr.Log(LOGLEVEL_DEBUG, "checking file %s", clientLocalCfgPath.c_str());
	ifstream		conf(clientLocalCfgPath.c_str());

	if (!conf) {
		string	err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_INFO, "can't open %s : %s", clientLocalCfgPath.c_str(), err.c_str());
		return false;
	}
	m_mgr.Log(LOGLEVEL_DEBUG, "reading file %s", clientLocalCfgPath.c_str());
	string		buf;
	while (!conf.eof()) {
		string		value;

		utils::GetConfigLine(conf, buf);
		if (utils::parseStrGetNext(buf, "svcautorestart:", value)) {
			m_mgr.Log(LOGLEVEL_DEBUG, "svcautorestart is %s", value.c_str());
			if (value == "on") m_autoReset = true;
		}
	}
	return false;
}

void					AgentSvcs::CheckSimpleService(SC_HANDLE & scm, LPCTSTR name, stringstream & reportData, 
													  stringstream & autoRestartReportData) {
	SC_HANDLE 				scs;
	LPQUERY_SERVICE_CONFIG 	lpServiceConfig;
	SERVICE_STATUS 			servStatus;
	DWORD 					bytesNeeded;
	BOOL 					retVal;
	TCHAR					tempName[MAX_SERVICE_LENGTH + 1];
	
	bytesNeeded = MAX_SERVICE_LENGTH;
	if ((GetServiceKeyName(scm, name, tempName, &bytesNeeded)) == 0) {
		return ;
	}
	tempName[bytesNeeded] = '\0';
	scs = OpenService(scm, tempName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
	if (scs == NULL) {
		string		sName = tempName;
		string 		mess = "can't open service " + sName;
		m_mgr.Log(LOGLEVEL_DEBUG, mess.c_str());
		return ;
	}
	retVal = QueryServiceConfig(scs, NULL, 0, &bytesNeeded);
	DWORD err = GetLastError();
	if ((retVal == FALSE) || err == ERROR_INSUFFICIENT_BUFFER) {
		DWORD dwBytes = bytesNeeded + sizeof(QUERY_SERVICE_CONFIG);
		
		try {
			lpServiceConfig = (LPQUERY_SERVICE_CONFIG)new unsigned char [dwBytes];
		} catch (std::bad_alloc ex) {
			CloseServiceHandle(scs);
			return ;
		}
		if (lpServiceConfig == NULL) {
			CloseServiceHandle(scs);
			return ;
		}
		if (QueryServiceConfig(scs, lpServiceConfig, dwBytes, &bytesNeeded) != 0 && 	
				QueryServiceStatus(scs, &servStatus) != 0) {
					map<string, SvcRule>::iterator	itr;

					itr = m_rules.find(name);
					if (itr != m_rules.end() && m_mgr.IsCentralModeEnabled() == false) { // treat the defined rule
						SvcRule	& rule = itr->second;
						if (rule.GetSvcState() != servStatus.dwCurrentState) {
							if (servStatus.dwCurrentState == SERVICE_STOPPED) { // service is stopped and should be running
								reportData << "&" << bbcolors[rule.GetAlarmColor()] << " " << name << " is stopped";
								if (rule.GetReset()) {
									StartService(scs, 0, NULL);
									reportData << " and will be restarted automatically" << endl;
								}
							} else if (servStatus.dwCurrentState == SERVICE_RUNNING) { // service is running and should be stopped
								SERVICE_STATUS		st;

								reportData << "&" << bbcolors[rule.GetAlarmColor()] << " " << name << " is running";
								if (rule.GetReset()) {
									ControlService(scs, SERVICE_CONTROL_STOP, &st);
									 reportData << " and will be stopped automatically" << endl;
								}
							} else {
								LPCSTR	str = (itr->second.GetSvcState() == SERVICE_RUNNING) ? "running" : "stoppped";
								reportData << "&" << bbcolors[rule.GetAlarmColor()] << " " << name << " should be " 
									<< str << " and is in an unmanaged state (" << findSvcStatus(servStatus.dwCurrentState) << ")";
							}
							if (m_pageColor < itr->second.GetAlarmColor())
								m_pageColor = itr->second.GetAlarmColor();
						} else { // service is ok
							LPCSTR		str = (servStatus.dwCurrentState == SERVICE_RUNNING) ? "running" : "stoppped";
							reportData << "&" << bbcolors[GREEN] << " " << name << " is ";
							reportData << str;
						}
						if (rule.GetComment().size() > 0)
							reportData << " (" << rule.GetComment() << ")";
						reportData << endl;
					} else { // no rules found
						// centralized mode 
						if (m_mgr.IsCentralModeEnabled()) {
							string		startType;
							switch (lpServiceConfig->dwStartType) {
								case SERVICE_AUTO_START: 
									startType = "automatic";
									break;
								case SERVICE_DISABLED:
									startType = "disabled";
									break;
								case SERVICE_DEMAND_START:
									startType = "manual";
									break;
								default:
									startType = "driver";
							}
							// replace spaces from servicename by underscore
							string			cleanName = tempName;
							std::replace(cleanName.begin(), cleanName.end(), ' ', '_');
							reportData << format("%-35s %-12s %-14s %s") 
								% cleanName
								% startType
								% findSvcStatus(servStatus.dwCurrentState)
								% name << endl;
						}
						if (m_autoReset) {
							if (servStatus.dwCurrentState == SERVICE_STOPPED 
								&& lpServiceConfig->dwStartType == SERVICE_AUTO_START) {
								StartService(scs, 0, NULL);
								if (m_mgr.IsCentralModeEnabled() == false)
									reportData << "&" << bbcolors[m_alarmColor] << " " << name << " is stopped and will be restarted automatically" << endl;
								else
									autoRestartReportData << "&yellow " << name << " is stopped and will be restarted automatically" << endl;
								if (m_pageColor < m_alarmColor)
									m_pageColor = m_alarmColor;
							} else if (servStatus.dwCurrentState != SERVICE_RUNNING 
								&& lpServiceConfig->dwStartType == SERVICE_AUTO_START) {
									if (m_mgr.IsCentralModeEnabled() == false)
										reportData << "&" << bbcolors[m_alarmColor] << " " << name
										<< " should be started and is in an unmanaged state (" << findSvcStatus(servStatus.dwCurrentState) << ")" << endl;
									else
										autoRestartReportData << "&yellow " << name
										<< " should be started and is in an unmanaged state (" << findSvcStatus(servStatus.dwCurrentState) << ")" << endl;
									if (m_pageColor < m_alarmColor)
										m_pageColor = m_alarmColor;
							}
						}
					}
		}
		delete [] lpServiceConfig;
	}
	CloseServiceHandle(scs);
}


// 
// CheckServices method
void				AgentSvcs::CheckServices(stringstream & reportData, stringstream & autoRestartReportData) {
	SC_HANDLE 				scm;
	ENUM_SERVICE_STATUS 	service_data, *lpservice;
	DWORD 					bytesNeeded, srvCount, resumeHandle = 0;
	BOOL 					retVal;
	
	if ((scm = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE)) == NULL) {
		return ;
	}
	retVal = ::EnumServicesStatus(scm, SERVICE_WIN32, SERVICE_STATE_ALL, &service_data, sizeof(service_data),
						&bytesNeeded, &srvCount, &resumeHandle);
	DWORD err = GetLastError();
    //Check if EnumServicesStatus needs more memory space
    if ((retVal == FALSE) || err == ERROR_MORE_DATA) {
		DWORD dwBytes = bytesNeeded + sizeof(ENUM_SERVICE_STATUS);
        
		try {
			lpservice = new ENUM_SERVICE_STATUS [dwBytes];
		} catch (std::bad_alloc ex) {
			CloseServiceHandle(scm);
		}
        EnumServicesStatus (scm, SERVICE_WIN32, SERVICE_STATE_ALL, lpservice, dwBytes,
								&bytesNeeded, &srvCount, &resumeHandle);
        for(DWORD inc = 0; inc < srvCount; inc++) {
			CheckSimpleService(scm, lpservice[inc].lpDisplayName, reportData, autoRestartReportData);
		}
		delete [] lpservice;
    }
	CloseServiceHandle(scm);
}

//
// Run method
// called from init
//
void AgentSvcs::Run() {
	stringstream 					reportData, autoRestartReportData;	

	if (m_mgr.IsCentralModeEnabled() == false) {
		DWORD							seconds;
		CSystemCounters					data;

		seconds = data.GetSystemUpTime();
		ptime now = second_clock::local_time();
		m_pageColor = GREEN;
		reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "] ";
		if (m_rules.size() == 0)
			reportData << "No service to check";
		if (m_autoReset) {
			reportData << " - Autoreset is On";
		}
		if (seconds < m_delay) {
			reportData << " - Computer has restarted in the last " << m_delay << " seconds\n\nno check is done until the delay is passed";
		} else {
			reportData << "\n" << endl;
			CheckServices(reportData, autoRestartReportData);
		}
		reportData << endl;
		if (m_alwaysGreen)
			m_pageColor = GREEN;
		m_mgr.Status(m_testName.c_str(), bbcolors[m_pageColor], reportData.str().c_str());
	} else {
		m_autoReset = false;
		InitCentralMode();
		reportData << format("%-35s %-12s %-14s %s") 
								% "Name"
								% "StartupType"
								% "Status"
								% "DisplayName" << endl;
		CheckServices(reportData, autoRestartReportData);
		m_mgr.ClientData(m_testName.c_str(), reportData.str().c_str());
		if (m_autoReset == true)
			m_mgr.ClientData("svcautorestart", autoRestartReportData.str().c_str());
	}
}

//
// init function
//
bool AgentSvcs::Init() {
	if (m_mgr.IsCentralModeEnabled() == false) {
		PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

		if (conf == NULL)
			return false;
		PBBWINCONFIGRANGE  range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string		name, value;

			name = m_mgr.GetConfigurationRangeValue(range, "name");
			value = m_mgr.GetConfigurationRangeValue(range, "value");
			if (name == "alwaysgreen") {
				if (value == "true")
					m_alwaysGreen = true;
			} else if (name == "autoreset") {
				if (value == "true") {
					cout << "autoreset" << endl;
					m_autoReset = true;
				}
			} else if (name == "testname") {
				if (value.length() > 0)
					m_testName = value;
			} else if (name == "delay") {
				if (value.length() > 0)
					m_delay = m_mgr.GetSeconds(value.c_str());
			} else if (name == "alarmcolor") {
				if (value.length() > 0) {
					if (value == "red") 
						m_alarmColor = RED;
					else if (value == "yellow") 
						m_alarmColor = YELLOW;
					else if (value == "green") 
						m_alarmColor = GREEN;
				}
			} else {
				string	autoreset = m_mgr.GetConfigurationRangeValue(range, "autoreset");
				string	color = m_mgr.GetConfigurationRangeValue(range, "alarmcolor");
				string	comment = m_mgr.GetConfigurationRangeValue(range, "comment");

				if (name.length() > 0 && value.length() > 0) {
					SvcRule		rule;
					map<string, SvcRule>::iterator		itr;

					rule.SetName(name);
					if (autoreset.length() > 0 && autoreset == "true") {
						rule.SetReset(true);
					}
					if (value.length() > 0 && value == "stopped") {
						rule.SetSvcState(SERVICE_STOPPED);
					} else  {
						rule.SetSvcState(SERVICE_RUNNING);
					}
					if (comment.length() > 0) 
						rule.SetComment(comment);
					if (color.length() > 0) {
						if (color == "red") 
							rule.SetAlarmColor(RED);
						else if (color == "yellow") 
							rule.SetAlarmColor(YELLOW);
						else if (color == "green") 
							rule.SetAlarmColor(GREEN);
					}
					itr = m_rules.find(name);
					if (itr == m_rules.end()) {
						m_rules.insert(pair<string, SvcRule>(name, rule));
					}
				}
			}
		}
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	return true;
}


//
// constructor 
//
AgentSvcs::AgentSvcs(IBBWinAgentManager & mgr) : 
		m_mgr(mgr),
		m_alarmColor(YELLOW),
		m_pageColor(GREEN),
		m_alwaysGreen(false),
		m_autoReset(false)
{
	m_testName = "svcs";
	m_delay = m_mgr.GetSeconds("5m");
}

//
// Destructor 
//
AgentSvcs::~AgentSvcs() {
}


//
// common agents export functions
//

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentSvcs(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &svcsAgentInfo;
}

