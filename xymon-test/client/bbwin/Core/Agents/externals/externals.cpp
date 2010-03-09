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

#include <windows.h>

#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
using namespace std;

#define BBWIN_AGENT_EXPORTS

#include "externals.h"

static const BBWinAgentInfo_t 		extAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"externals",    					// agentName;
	"externals agent : execute independant big brother hobbit scripts",        // agentDescription;
	0									// flags
};                

void 			AgentExternals::SendExternalReport(const string & reportName, const string & reportPath) {
	ifstream 	report(reportPath.c_str());
	TCHAR		buf[1024];
	size_t		pos;
	string		color;
	string 		lifeTime;
	stringstream 	dbgMess;

    if (report) {
		dbgMess << "sending report '" << reportName << "' " << reportPath;
		string 			statusLine; 
		stringstream 	reportData;	
        
		getline(report, statusLine );
		pos = statusLine.find_first_of(" ");
		reportData << "status";
		if (pos > 0 && pos < statusLine.length()) {
			color = statusLine.substr(0, pos);
			statusLine.erase(0, pos);
			pos = color.find_first_of("+");
			if (pos > 0 && pos < statusLine.length()) {
				lifeTime = color.substr(pos, color.length());
				color = color.substr(0, pos);
				reportData << lifeTime;
			}
		}
		// check the validity of the line
		reportData << " " << m_mgr.GetSetting("hostname") << "." << reportName << " ";
		reportData << color << " ";
		reportData << statusLine << endl;
        reportData << report.rdbuf();
		report.close();
		m_mgr.Message(reportData.str().c_str(), buf, 1024);
    }
}

void			AgentExternals::SendExternalsReports() {
	WIN32_FIND_DATA 	FindFileData;
	HANDLE 				hFind = INVALID_HANDLE_VALUE;
	string 				dirSpec;  // directory specification
	DWORD 				dwError;

	dirSpec += m_mgr.GetSetting("tmppath") + "\\*";
	hFind = FindFirstFile(dirSpec.c_str(), &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE) {
		TCHAR	buf[ERROR_STR_LEN + 1];
		string err;

		m_mgr.GetLastErrorString(buf, ERROR_STR_LEN);
		err = buf;
		string mess = "can't get file listing from tmp path " + m_mgr.GetSetting("tmppath") + ": " + err;
		m_mgr.ReportEventError(mess.c_str());
		return ;
	} else {
		string		reportName;
		string		reportPath;
		size_t		res;
		
		while (FindNextFile(hFind, &FindFileData) != 0) {
			reportName = FindFileData.cFileName;
			res = reportName.find(".");
			if (res < 0 || res > reportName.size()) {
				reportPath = m_mgr.GetSetting("tmppath") + "\\" + reportName;
				SendExternalReport(reportName, reportPath);
				DeleteFile(reportPath.c_str());
			}
		}
		dwError = GetLastError();
		FindClose(hFind);
		if (dwError != ERROR_NO_MORE_FILES) 
		{
			return ;
		}
	}
}

void 	AgentExternals::Run() {
	// launch externals
	vector<External *>::iterator		it;
	for (it = m_externals.begin(); it != m_externals.end(); ++it)  {
		(*it)->start();
	}
	for (;;) {
		DWORD 		dwWait;
		
		dwWait = WaitForMultipleObjects(m_hCount, m_hEvents , FALSE, m_logsTimer * 1000 );
		if ( dwWait >= WAIT_OBJECT_0 && dwWait <= (WAIT_OBJECT_0 + m_hCount - 1) ) {    
			break ;
		} else if (dwWait >= WAIT_ABANDONED_0 && dwWait <= (WAIT_ABANDONED_0 + m_hCount - 1) ) {
			break ;
		}
		SendExternalsReports();
	}
	for (it = m_externals.begin(); it != m_externals.end(); ++it)  {
		(*it)->stop();
	}
	for (it = m_externals.begin(); it != m_externals.end(); ++it)  {
		delete (*it);
	}
	m_externals.clear();
	delete [] m_hEvents;
}

AgentExternals::AgentExternals(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_timer = m_mgr.GetNbr(m_mgr.GetSetting("timer").c_str());
	m_hCount = m_mgr.GetHandlesCount();
	try {
		m_hEvents = new HANDLE[m_hCount];
	} catch (std::bad_alloc ex) {
		m_hEvents = NULL;
	}
	m_mgr.GetHandles(m_hEvents);
	m_logsTimer = EXTERNAL_DEFAULT_LOG_TIMER;
}

bool		AgentExternals::Init() {
	 PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());
	
	if (conf == NULL)
		return false;
	if (m_hEvents == NULL)
		return false;
	PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
	if (range == NULL)
		return false;
	do {
		string		name, value;

		name = m_mgr.GetConfigurationRangeValue(range, "name");
		value = m_mgr.GetConfigurationRangeValue(range, "value");		
		if (name == "timer" && value.length() > 0) {
			DWORD timer = m_mgr.GetSeconds(value.c_str());
			if (timer < 5 || timer > 2678400)
				m_mgr.ReportEventWarn("Setting timer must be between 5 seconds and 31 days(will use default setting (30 seconds) until you check your configuration)");
			else 
				m_timer = m_mgr.GetSeconds(value.c_str());
		}
		if (name == "logstimer" && value.length() > 0) {
			DWORD logTimer = m_mgr.GetSeconds(value.c_str());
			if (logTimer < 5 || logTimer > 600)
				m_mgr.ReportEventWarn("Setting logstimer must be between 5 seconds and 10 minutes (will use default setting (30 seconds) until you check your configuration)");
			else 
				m_logsTimer = m_mgr.GetSeconds(value.c_str());
		}
	}	
	while (m_mgr.IterateConfigurationRange(range));
	m_mgr.FreeConfigurationRange(range);
	range = m_mgr.GetConfigurationRange(conf, "load");
	for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
		string		timer, value;
		
		timer = m_mgr.GetConfigurationRangeValue(range, "timer");
		value = m_mgr.GetConfigurationRangeValue(range, "value");	
		if (value.length() > 0) {
			External		*ext;
			DWORD			dTimer = m_timer;
			if (timer.length() > 0) {
				dTimer = m_mgr.GetSeconds(timer.c_str());
				if (dTimer < 5 || dTimer > 2678400) {
					string 	mess = "Setting timer ";
					mess += value;
					mess += "must be between 5 seconds and 31 days(will use default setting (30 seconds) until you check your configuration)";
					m_mgr.ReportEventWarn(mess.c_str());
				} else {
					dTimer = m_mgr.GetSeconds(timer.c_str());
				}
			}
			try {
				ext = new External(m_mgr, value, dTimer);
			} catch (std::bad_alloc ex) {
				continue ;
			}
			if (ext == NULL)
				continue ;
			m_externals.push_back(ext);
		}
	}
	if (m_externals.size() == 0) {
		m_mgr.ReportEventWarn("No externals have been specified");
	}
	m_mgr.FreeConfigurationRange(range);
	m_mgr.FreeConfiguration(conf);
	return true;
}

void 	* AgentExternals::operator new (size_t sz) {
	return ::new unsigned char [sz];
}

void 	AgentExternals::operator delete (void * ptr) {
	::delete [] (unsigned char *)ptr;
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentExternals(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &extAgentInfo;
}
