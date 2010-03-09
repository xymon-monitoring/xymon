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
// $Id: who.cpp 83 2008-02-11 17:35:53Z sharpyy $

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <assert.h>
#include <lm.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"
#include "who.h"

using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;
using namespace std;

#define MAX_NAME_STRING   1024

static const BBWinAgentInfo_t 		whoAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"who",    					// agentName;
	"list current connected users",        // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE			// flags
};                

void 		AgentWho::Run() {
	stringstream 	reportData;	
	
	if (m_mgr.IsCentralModeEnabled() == false) {
		ptime now = second_clock::local_time();
		reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "]" << endl;
	}
	TCHAR		buf[TEMP_PATH_LEN + 1];
	string			execCmd, path;

	m_mgr.GetEnvironmentVariable("TEMP", buf, TEMP_PATH_LEN);
	path = buf;
	path += "\\qwinsta.tmp";

	execCmd = "qwinsta > " + path;
	system(execCmd.c_str());
	ifstream fileOut(path.c_str());
	if (fileOut) {
		string 		line;
		string 		prefix;

		while (getline(fileOut, line)) {
			size_t i;

			// trunk space
			for (i = 0; i < line.size() && (line[i] == ' ' || line[i] == '\t'); ++i)
				;
			if (i == (line.size() - 1))
				continue ;
			reportData << line.substr(i) << endl;
		}
	} else {
		string err = (string)"failed to open report file " + path;
		m_mgr.Log(LOGLEVEL_ERROR,err.c_str());
		m_mgr.ReportEventError(err.c_str());
	}
	if (m_mgr.IsCentralModeEnabled())
		m_mgr.ClientData(m_testName.c_str(), reportData.str().c_str());
	else
		m_mgr.Status(m_testName.c_str(), "green", reportData.str().c_str());
}

AgentWho::AgentWho(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_testName = "who";
}

bool		AgentWho::Init() {
	if (m_mgr.IsCentralModeEnabled() == false) {
		PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

		if (conf == NULL)
			return false;
		PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string name, value;

			name = m_mgr.GetConfigurationRangeValue(range, "name");
			value = m_mgr.GetConfigurationRangeValue(range, "value");
			if (name == "testname") {
				m_testName = value;
			}
		}	
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	return true;
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentWho(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &whoAgentInfo;
}
